#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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

#define ADV_DURATION_MS 350

static SemaphoreHandle_t s_mutex = NULL;
static SemaphoreHandle_t s_ready_sem = NULL;
static SemaphoreHandle_t s_adv_done_sem = NULL;
static bool s_initialized = false;
static bool s_ready = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint8_t s_mac[6];
static uint32_t s_counter = 0;
static psa_key_id_t s_key_id = PSA_KEY_ID_NULL;

typedef struct {
    uint32_t generation;
    bool completed;
    bool reset;
    int reason;
} adv_operation_t;

static uint32_t s_adv_generation = 0;
static adv_operation_t *s_active_adv_operation = NULL;

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

static void reset_adv_done_signal(void)
{
    reset_signal(s_adv_done_sem);
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

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    adv_operation_t *operation = s_active_adv_operation;
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        if (!operation || generation != operation->generation) {
            return 0;
        }

        operation->reason = event->adv_complete.reason;
        operation->reset = false;
        operation->completed = true;
        if (s_adv_done_sem) {
            xSemaphoreGive(s_adv_done_sem);
        }
    }
    return 0;
}

static void on_reset(int reason)
{
    adv_operation_t *operation = s_active_adv_operation;

    ESP_LOGW(TAG, "BLE host reset: %d", reason);
    s_ready = false;
    reset_ready_signal();
    if (s_ready_sem) {
        xSemaphoreGive(s_ready_sem);
    }

    if (operation) {
        operation->reason = reason;
        operation->reset = true;
        operation->completed = true;
        if (s_adv_done_sem) {
            xSemaphoreGive(s_adv_done_sem);
        }
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
    return s_initialized && s_mutex && s_ready_sem && s_adv_done_sem;
}

static void adv_operation_begin(adv_operation_t *operation)
{
    if (!operation) {
        return;
    }

    operation->generation = ++s_adv_generation;
    operation->completed = false;
    operation->reset = false;
    operation->reason = BLE_HS_EUNKNOWN;
    reset_adv_done_signal();
    s_active_adv_operation = operation;
}

static void adv_operation_end(adv_operation_t *operation)
{
    if (s_active_adv_operation == operation) {
        s_active_adv_operation = NULL;
    }
}

static esp_err_t adv_operation_wait(adv_operation_t *operation, TickType_t timeout_ticks)
{
    if (!operation) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_adv_done_sem, timeout_ticks) != pdTRUE) {
        adv_operation_end(operation);
        ESP_LOGW(TAG, "BLE advertising completion timeout");
        return ESP_ERR_TIMEOUT;
    }

    adv_operation_end(operation);

    if (operation->reset) {
        ESP_LOGW(TAG, "BLE advertising aborted by host reset: %d", operation->reason);
        return ESP_ERR_INVALID_STATE;
    }

    if (!operation->completed) {
        ESP_LOGW(TAG, "BLE advertising completion missing");
        return ESP_ERR_TIMEOUT;
    }

    if (operation->reason != BLE_HS_ETIMEOUT) {
        ESP_LOGW(TAG, "BLE advertising ended unexpectedly: %d", operation->reason);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t start_adv_operation(adv_operation_t *operation,
                                     const struct ble_gap_adv_params *adv_params)
{
    int rc;

    adv_operation_begin(operation);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, ADV_DURATION_MS, adv_params,
                           gap_event_cb, (void *)(uintptr_t)operation->generation);
    if (rc != 0) {
        adv_operation_end(operation);
        if (!s_ready) {
            ESP_LOGW(TAG, "ble_gap_adv_start interrupted by host reset: %d", rc);
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    return adv_operation_wait(operation, pdMS_TO_TICKS(ADV_DURATION_MS + 1000));
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t encrypt_payload(const uint8_t plaintext[4],
                                 uint32_t counter,
                                 uint8_t out_service_data[2 + 1 + 4 + 4 + 4],
                                 size_t *out_len)
{
    uint8_t counter_le[4];
    uint8_t nonce[13];
    uint8_t ciphertext_and_tag[8];
    size_t ct_len = 0;
    psa_status_t status;

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
                              plaintext, 4,
                              ciphertext_and_tag, sizeof(ciphertext_and_tag),
                              &ct_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_encrypt failed: %d", (int)status);
        return ESP_FAIL;
    }

    out_service_data[0] = BTHOME_UUID_LO;
    out_service_data[1] = BTHOME_UUID_HI;
    out_service_data[2] = BTHOME_DEVICE_INFO_ENCRYPTED_V2;
    memcpy(&out_service_data[3], ciphertext_and_tag, 4);
    memcpy(&out_service_data[7], counter_le, 4);
    memcpy(&out_service_data[11], &ciphertext_and_tag[4], 4);
    *out_len = 15;
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
    s_adv_done_sem = xSemaphoreCreateBinary();
    if (!s_mutex || !s_ready_sem || !s_adv_done_sem) {
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

esp_err_t ble_button_tx_send_event(button_event_t event)
{
    uint8_t plaintext[4];
    uint8_t service_data[15];
    size_t service_data_len = 0;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    adv_operation_t operation;
    uint8_t event_code = 0;
    uint32_t next_counter = 0;
    esp_err_t err = ESP_OK;
    int rc;

    switch (event) {
        case BUTTON_EVENT_SINGLE_PRESS:
        case BUTTON_EVENT_DOUBLE_PRESS:
        case BUTTON_EVENT_TRIPLE_PRESS:
        case BUTTON_EVENT_LONG_PRESS:
            event_code = (uint8_t)event;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    if (!tx_runtime_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }

    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.svc_data_uuid16 = service_data;

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    for (int attempt = 0; attempt < 2; attempt++) {
        err = wait_until_ready_ticks(pdMS_TO_TICKS(3000));
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }

        next_counter = s_counter + 1;
        plaintext[0] = BTHOME_OBJ_PACKET_ID;
        plaintext[1] = (uint8_t)(next_counter & 0xFF);
        plaintext[2] = BTHOME_OBJ_BUTTON;
        plaintext[3] = event_code;

        err = encrypt_payload(plaintext, next_counter, service_data, &service_data_len);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }
        adv_fields.svc_data_uuid16_len = service_data_len;

        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }

        rc = ble_gap_adv_set_fields(&adv_fields);
        if (rc != 0) {
            if (!s_ready && attempt == 0) {
                continue;
            }
            ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
            xSemaphoreGive(s_mutex);
            return ESP_FAIL;
        }

        err = counter_save(next_counter);
        if (err != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return err;
        }
        s_counter = next_counter;

        err = start_adv_operation(&operation, &adv_params);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "sent button event=%u counter=%" PRIu32, (unsigned)event, s_counter);
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
