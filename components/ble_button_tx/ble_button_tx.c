#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include "ble_button_tx.h"

static const char *TAG = "ble_button_tx";
static const char *NVS_NS = "ble_button";
static const char *NVS_KEY_COUNTER = "counter";

#define BTHOME_UUID_LO 0xD2
#define BTHOME_UUID_HI 0xFC
#define BTHOME_DEVICE_INFO_ENCRYPTED_V2 0x41
#define BTHOME_OBJ_PACKET_ID 0x00
#define BTHOME_OBJ_BUTTON    0x3A
#define BTHOME_BUTTON_EVENT_NONE 0x00

#define BTHOME_BUTTON_OBJECT_LEN 2
#define BTHOME_MAX_BUTTON_OBJECT_COUNT BLE_BUTTON_TX_MAX_BUTTONS
#define BTHOME_PLAINTEXT_BASE_LEN 2
#define BTHOME_MAX_PLAINTEXT_LEN (BTHOME_PLAINTEXT_BASE_LEN + \
                                  (BTHOME_MAX_BUTTON_OBJECT_COUNT * BTHOME_BUTTON_OBJECT_LEN))
#define BTHOME_COUNTER_LEN 4
#define BTHOME_NONCE_LEN 13
#define BTHOME_TAG_LEN 4
#define BTHOME_MAX_CIPHERTEXT_AND_TAG_LEN (BTHOME_MAX_PLAINTEXT_LEN + BTHOME_TAG_LEN)
#define BTHOME_MAX_SERVICE_DATA_LEN (2 + 1 + BTHOME_MAX_PLAINTEXT_LEN + BTHOME_COUNTER_LEN + BTHOME_TAG_LEN)
#define BLE_ADV_MAX_PAYLOAD_LEN 31
#define BLE_ADV_FIELD_OVERHEAD_LEN 2
#define BLE_ADV_FLAGS_FIELD_TOTAL_LEN 3
#define BLE_ADV_SERVICE_DATA_FIELD_TOTAL_LEN (BTHOME_MAX_SERVICE_DATA_LEN + BLE_ADV_FIELD_OVERHEAD_LEN)

_Static_assert(BLE_ADV_FLAGS_FIELD_TOTAL_LEN + BLE_ADV_SERVICE_DATA_FIELD_TOTAL_LEN <=
                   BLE_ADV_MAX_PAYLOAD_LEN,
               "BTHome advertisement exceeds legacy ADV payload budget");

#define ADV_DURATION_MS 200
#define ADV_STOP_GRACE_MS 50
#define ADV_WAIT_POLL_MS 20
#define ADV_STOP_TIMEOUT_MS (ADV_DURATION_MS + ADV_STOP_GRACE_MS)
#define ADV_WAIT_TIMEOUT_MS (ADV_STOP_TIMEOUT_MS + ADV_WAIT_POLL_MS)
#define BLE_READY_TIMEOUT_MS 3000

static SemaphoreHandle_t s_mutex = NULL;
static SemaphoreHandle_t s_ready_sem = NULL;
static TimerHandle_t s_adv_stop_timer = NULL;
static bool s_initialized = false;
static bool s_ready = false;
static bool s_adv_active = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint8_t s_mac[6];
static uint32_t s_counter = 0;
static uint32_t s_adv_generation = 0;
static uint32_t s_active_adv_generation = 0;
static TickType_t s_adv_deadline_ticks = 0;
static psa_key_id_t s_key_id = PSA_KEY_ID_NULL;

static void reset_signal(SemaphoreHandle_t sem)
{
    if (!sem) {
        return;
    }

    while (xSemaphoreTake(sem, 0) == pdTRUE) {
    }
}

static void reset_ready_signal(void)
{
    reset_signal(s_ready_sem);
}

static TickType_t adv_wait_ticks_until_deadline(void)
{
    const TickType_t now = xTaskGetTickCount();
    const int32_t remaining_ticks = (int32_t)(s_adv_deadline_ticks - now);

    return remaining_ticks > 0 ? (TickType_t)remaining_ticks : 0;
}

static void stop_adv_stop_timer(void)
{
    if (s_adv_stop_timer) {
        xTimerStop(s_adv_stop_timer, 0);
    }
}

static void clear_adv_state(void)
{
    s_adv_active = false;
    s_adv_deadline_ticks = 0;
}

static esp_err_t stop_active_advertising(uint32_t expected_generation, bool stop_timer)
{
    int rc;

    if (!s_adv_active) {
        return ESP_OK;
    }

    if (expected_generation != 0 && expected_generation != s_active_adv_generation) {
        return ESP_OK;
    }

    rc = ble_gap_adv_stop();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        if (stop_timer) {
            stop_adv_stop_timer();
        }
        clear_adv_state();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "ble_gap_adv_stop failed: %d", rc);
    return ESP_FAIL;
}

static void adv_stop_timer_cb(TimerHandle_t timer)
{
    const uint32_t generation = (uint32_t)(uintptr_t)pvTimerGetTimerID(timer);

    if (!s_adv_active || generation != s_active_adv_generation) {
        return;
    }

    ESP_LOGI(TAG, "BLE advertising deadline reached locally after %d ms; stopping explicitly",
             ADV_DURATION_MS + ADV_STOP_GRACE_MS);
    (void)stop_active_advertising(generation, false);
}

static esp_err_t arm_adv_stop_timer(uint32_t generation)
{
    if (!s_adv_stop_timer) {
        return ESP_ERR_INVALID_STATE;
    }

    vTimerSetTimerID(s_adv_stop_timer, (void *)(uintptr_t)generation);
    if (xTimerChangePeriod(s_adv_stop_timer,
                           pdMS_TO_TICKS(ADV_STOP_TIMEOUT_MS),
                           0) != pdPASS) {
        ESP_LOGE(TAG, "failed to arm advertising stop timer");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t counter_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u32(handle, NVS_KEY_COUNTER, &s_counter);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_counter = 0;
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

static esp_err_t counter_save(uint32_t counter)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, NVS_KEY_COUNTER, counter);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t import_key(const uint8_t key[16])
{
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t status;

    if (s_key_id != PSA_KEY_ID_NULL) {
        psa_destroy_key(s_key_id);
        s_key_id = PSA_KEY_ID_NULL;
    }

    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attrs, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4));
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, 128);

    status = psa_import_key(&attrs, key, 16, &s_key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %d", (int)status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t button_event_to_event_code(button_event_t event, uint8_t *out_event_code)
{
    if (!out_event_code) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (event) {
        case BUTTON_EVENT_SINGLE_PRESS:
        case BUTTON_EVENT_DOUBLE_PRESS:
        case BUTTON_EVENT_TRIPLE_PRESS:
        case BUTTON_EVENT_LONG_PRESS:
            *out_event_code = (uint8_t)event;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static size_t plaintext_len_for_button_count(size_t button_count)
{
    return BTHOME_PLAINTEXT_BASE_LEN + (button_count * BTHOME_BUTTON_OBJECT_LEN);
}

static size_t service_data_len_for_plaintext_len(size_t plaintext_len)
{
    return 2 + 1 + plaintext_len + BTHOME_COUNTER_LEN + BTHOME_TAG_LEN;
}

static esp_err_t encode_button_objects(uint8_t *plaintext,
                                       button_event_t event,
                                       size_t active_button,
                                       size_t total_buttons)
{
    size_t offset = BTHOME_PLAINTEXT_BASE_LEN;
    uint8_t active_event_code;
    esp_err_t err;

    if (!plaintext ||
        total_buttons == 0 ||
        total_buttons > BTHOME_MAX_BUTTON_OBJECT_COUNT ||
        active_button == 0 ||
        active_button > total_buttons) {
        return ESP_ERR_INVALID_ARG;
    }

    err = button_event_to_event_code(event, &active_event_code);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t button = 1; button <= total_buttons; button++) {
        plaintext[offset++] = BTHOME_OBJ_BUTTON;
        plaintext[offset++] = (button == active_button) ? active_event_code : BTHOME_BUTTON_EVENT_NONE;
    }

    return ESP_OK;
}

static void init_adv_fields(struct ble_hs_adv_fields *adv_fields,
                            uint8_t *service_data)
{
    memset(adv_fields, 0, sizeof(*adv_fields));
    adv_fields->flags = BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields->svc_data_uuid16 = service_data;
}

static void init_adv_params(struct ble_gap_adv_params *adv_params)
{
    memset(adv_params, 0, sizeof(*adv_params));
    adv_params->conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params->disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params->itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params->itvl_max = BLE_GAP_ADV_ITVL_MS(50);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
    s_ready = false;
    stop_adv_stop_timer();
    clear_adv_state();
    reset_ready_signal();
    if (s_ready_sem) {
        xSemaphoreGive(s_ready_sem);
    }
}

static void on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_own_addr_type, s_mac, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_copy_addr failed: %d", rc);
        return;
    }

    s_ready = true;
    reset_ready_signal();
    xSemaphoreGive(s_ready_sem);

    char mac_hex[18];
    snprintf(mac_hex, sizeof(mac_hex), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_mac[5], s_mac[4], s_mac[3], s_mac[2], s_mac[1], s_mac[0]);
    ESP_LOGI(TAG, "BLE ready, advertiser MAC=%s own_addr_type=%u", mac_hex, s_own_addr_type);
}

static esp_err_t wait_until_ready_ticks(TickType_t timeout_ticks)
{
    const TickType_t start_ticks = xTaskGetTickCount();

    while (!s_ready) {
        TickType_t elapsed_ticks = xTaskGetTickCount() - start_ticks;
        TickType_t remaining_ticks;

        if (elapsed_ticks >= timeout_ticks) {
            ESP_LOGW(TAG, "BLE sync timeout");
            return ESP_ERR_TIMEOUT;
        }

        remaining_ticks = timeout_ticks - elapsed_ticks;
        if (xSemaphoreTake(s_ready_sem, remaining_ticks) != pdTRUE) {
            ESP_LOGW(TAG, "BLE sync timeout");
            return ESP_ERR_TIMEOUT;
        }

        if (!s_ready) {
            ESP_LOGW(TAG, "BLE sync interrupted by reset; retrying");
        }
    }

    return ESP_OK;
}

static bool tx_runtime_ready(void)
{
    return s_initialized && s_mutex && s_ready_sem && s_adv_stop_timer;
}

static esp_err_t start_adv_operation(const struct ble_gap_adv_params *adv_params)
{
    esp_err_t err;
    int rc;
    const uint32_t generation = ++s_adv_generation;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, ADV_DURATION_MS, adv_params,
                           NULL, NULL);
    if (rc != 0) {
        clear_adv_state();
        if (!s_ready) {
            ESP_LOGW(TAG, "ble_gap_adv_start interrupted by host reset: %d", rc);
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    s_active_adv_generation = generation;
    s_adv_active = true;
    s_adv_deadline_ticks = xTaskGetTickCount() +
                           pdMS_TO_TICKS(ADV_STOP_TIMEOUT_MS);
    err = arm_adv_stop_timer(generation);
    if (err != ESP_OK) {
        (void)stop_active_advertising(generation, false);
        return err;
    }

    ESP_LOGI(TAG, "BLE advertising started for %d ms", ADV_DURATION_MS);
    return ESP_OK;
}

esp_err_t ble_button_tx_wait_for_adv_complete(void)
{
    TickType_t elapsed_ticks = 0;
    TickType_t wait_ticks;
    uint32_t wait_generation;

    if (!tx_runtime_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    wait_generation = s_active_adv_generation;
    if (!s_adv_active || wait_generation == 0) {
        return ESP_OK;
    }

    while (s_adv_active && s_active_adv_generation == wait_generation) {
        wait_ticks = adv_wait_ticks_until_deadline();
        if (wait_ticks == 0 || wait_ticks > pdMS_TO_TICKS(ADV_WAIT_POLL_MS)) {
            wait_ticks = pdMS_TO_TICKS(ADV_WAIT_POLL_MS);
        }

        vTaskDelay(wait_ticks);
        elapsed_ticks += wait_ticks;

        if (elapsed_ticks > pdMS_TO_TICKS(ADV_WAIT_TIMEOUT_MS)) {
            break;
        }
    }

    if (!s_adv_active || s_active_adv_generation != wait_generation) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "BLE advertising session %" PRIu32 " outlived the local stop timer; forcing stop",
             wait_generation);
    return stop_active_advertising(wait_generation, true);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t encrypt_payload(const uint8_t *plaintext,
                                 size_t plaintext_len,
                                 uint32_t counter,
                                 uint8_t out_service_data[BTHOME_MAX_SERVICE_DATA_LEN],
                                 size_t *out_len)
{
    uint8_t counter_le[BTHOME_COUNTER_LEN];
    uint8_t nonce[BTHOME_NONCE_LEN];
    uint8_t ciphertext_and_tag[BTHOME_MAX_CIPHERTEXT_AND_TAG_LEN];
    size_t ct_len = 0;
    psa_status_t status;

    if (!plaintext || !out_service_data || !out_len ||
        plaintext_len < BTHOME_PLAINTEXT_BASE_LEN ||
        plaintext_len > BTHOME_MAX_PLAINTEXT_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(counter_le, &counter, sizeof(counter_le));

    for (int i = 0; i < 6; i++) {
        nonce[i] = s_mac[5 - i];
    }
    nonce[6] = BTHOME_UUID_LO;
    nonce[7] = BTHOME_UUID_HI;
    nonce[8] = BTHOME_DEVICE_INFO_ENCRYPTED_V2;
    memcpy(&nonce[9], counter_le, sizeof(counter_le));

    status = psa_aead_encrypt(s_key_id,
                              PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4),
                              nonce, sizeof(nonce),
                              NULL, 0,
                              plaintext, plaintext_len,
                              ciphertext_and_tag, sizeof(ciphertext_and_tag),
                              &ct_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_encrypt failed: %d", (int)status);
        return ESP_FAIL;
    }

    out_service_data[0] = BTHOME_UUID_LO;
    out_service_data[1] = BTHOME_UUID_HI;
    out_service_data[2] = BTHOME_DEVICE_INFO_ENCRYPTED_V2;
    memcpy(&out_service_data[3], ciphertext_and_tag, plaintext_len);
    memcpy(&out_service_data[3 + plaintext_len], counter_le, BTHOME_COUNTER_LEN);
    memcpy(&out_service_data[3 + plaintext_len + BTHOME_COUNTER_LEN],
           &ciphertext_and_tag[plaintext_len],
           BTHOME_TAG_LEN);
    *out_len = service_data_len_for_plaintext_len(plaintext_len);
    return ESP_OK;
}

esp_err_t ble_button_tx_init(const uint8_t key[16])
{
    psa_status_t crypto_status;
    esp_err_t err;

    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    s_ready_sem = xSemaphoreCreateBinary();
    s_adv_stop_timer = xTimerCreate("adv_stop",
                                    pdMS_TO_TICKS(ADV_STOP_TIMEOUT_MS),
                                    pdFALSE,
                                    NULL,
                                    adv_stop_timer_cb);
    if (!s_mutex || !s_ready_sem || !s_adv_stop_timer) {
        return ESP_ERR_NO_MEM;
    }

    crypto_status = psa_crypto_init();
    if (crypto_status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)crypto_status);
        return ESP_FAIL;
    }

    err = import_key(key);
    if (err != ESP_OK) {
        return err;
    }

    err = counter_load();
    if (err != ESP_OK) {
        return err;
    }

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t ble_button_tx_send_event(button_event_t event,
                                   size_t active_button,
                                   size_t total_buttons)
{
    uint8_t plaintext[BTHOME_MAX_PLAINTEXT_LEN];
    uint8_t service_data[BTHOME_MAX_SERVICE_DATA_LEN];
    size_t plaintext_len;
    size_t service_data_len = 0;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    uint32_t next_counter = 0;
    esp_err_t err = ESP_OK;
    int rc;

    if (total_buttons == 0 ||
        total_buttons > BTHOME_MAX_BUTTON_OBJECT_COUNT ||
        active_button == 0 ||
        active_button > total_buttons) {
        return ESP_ERR_INVALID_ARG;
    }
    plaintext_len = plaintext_len_for_button_count(total_buttons);

    if (!tx_runtime_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_adv_active) {
        err = stop_active_advertising(0, true);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }
    }

    init_adv_fields(&adv_fields, service_data);
    init_adv_params(&adv_params);

    for (int attempt = 0; attempt < 2; attempt++) {
        err = wait_until_ready_ticks(pdMS_TO_TICKS(BLE_READY_TIMEOUT_MS));
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }

        next_counter = s_counter + 1;
        plaintext[0] = BTHOME_OBJ_PACKET_ID;
        plaintext[1] = (uint8_t)(next_counter & 0xFF);
        err = encode_button_objects(plaintext, event, active_button, total_buttons);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }

        err = encrypt_payload(plaintext, plaintext_len, next_counter, service_data, &service_data_len);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }
        adv_fields.svc_data_uuid16_len = service_data_len;

        rc = ble_gap_adv_set_fields(&adv_fields);
        if (rc != 0) {
            if (!s_ready && attempt == 0) {
                continue;
            }
            ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
            xSemaphoreGive(s_mutex);
            return ESP_FAIL;
        }

        /* Persist before advertising so resets cannot reuse a packet counter. */
        err = counter_save(next_counter);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }
        s_counter = next_counter;

        err = start_adv_operation(&adv_params);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "sent button event=%u active_button=%u total_buttons=%u counter=%" PRIu32,
                     (unsigned)event,
                     (unsigned)active_button,
                     (unsigned)total_buttons,
                     s_counter);
            xSemaphoreGive(s_mutex);
            return ESP_OK;
        }

        if (err == ESP_ERR_INVALID_STATE && attempt == 0) {
            continue;
        }

        xSemaphoreGive(s_mutex);
        return err;
    }

    xSemaphoreGive(s_mutex);
    return ESP_ERR_TIMEOUT;
}
