#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "ble_button_tx.h"
#include "device_identity.h"
#include "gpio_manager.h"
#include "led_feedback.h"
#include "system_runtime.h"

static const char *TAG = "system_runtime";

typedef struct {
    bool ble_ready_for_event;
} wake_capture_runtime_t;

static void init_nvs_or_die(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS init failed with %s; refusing to erase persisted identity automatically",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(err);
}

static void print_registration_credentials(void)
{
    uint8_t mac[6];
    char key_hex[33];

    if (device_identity_get_mac(mac) != ESP_OK) {
        ESP_LOGW(TAG, "device identity MAC not ready");
        return;
    }

    device_identity_format_key_hex(key_hex);

    ESP_LOGI(TAG, "==== BluButton Registration Credentials ====");
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    ESP_LOGI(TAG, "AES Key: %s", key_hex);
    ESP_LOGI(TAG, "===========================================");
}

static void enter_deep_sleep(void) __attribute__((noreturn));

static void enter_deep_sleep(void)
{
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(gpio_manager_enable_boot_button_wakeup());
    esp_deep_sleep_start();
}

static esp_err_t init_runtime_during_wake_capture(void *ctx)
{
    wake_capture_runtime_t *runtime = ctx;
    esp_err_t err = ble_button_tx_init(device_identity_get_key());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BLE init failed at wake start: %s", esp_err_to_name(err));
        runtime->ble_ready_for_event = false;
        return ESP_OK;
    }

    runtime->ble_ready_for_event = true;
    return ESP_OK;
}

void system_runtime_init(void)
{
    esp_err_t err;
    button_event_t event = BUTTON_EVENT_SINGLE_PRESS;
    bool have_event = false;
    const uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();
    wake_capture_runtime_t wake_runtime = {
        .ble_ready_for_event = false,
    };

    init_nvs_or_die();
    ESP_ERROR_CHECK(device_identity_init());
    err = led_feedback_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED feedback unavailable: %s", esp_err_to_name(err));
    }

    if (device_identity_key_was_created()) {
        print_registration_credentials();
    }

    ESP_ERROR_CHECK(gpio_manager_capture_wake_event(wakeup_causes,
                                                    init_runtime_during_wake_capture,
                                                    &wake_runtime,
                                                    &event,
                                                    &have_event));

    if (have_event) {
        switch (event) {
            case BUTTON_EVENT_SINGLE_PRESS:
            case BUTTON_EVENT_DOUBLE_PRESS:
            case BUTTON_EVENT_TRIPLE_PRESS:
            case BUTTON_EVENT_LONG_PRESS:
                if (wake_runtime.ble_ready_for_event) {
                    err = ble_button_tx_send_event(event);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "BLE send failed for event %d: %s", (int)event, esp_err_to_name(err));
                    }
                }

                err = led_feedback_run_button_event_pattern_blocking(event);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "LED feedback failed for event %d: %s", (int)event, esp_err_to_name(err));
                }
                break;
            case BUTTON_EVENT_MAINTENANCE_HOLD:
                print_registration_credentials();
                vTaskDelay(pdMS_TO_TICKS(250));
                break;
            default:
                break;
        }
    }

    enter_deep_sleep();
}
