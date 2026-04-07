#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ble_button_tx.h"
#include "device_identity.h"
#include "gpio_manager.h"
#include "led_feedback.h"
#include "system_runtime.h"

static const char *TAG = "system_runtime";
#define EVENT_QUEUE_LEN 8

static QueueHandle_t s_event_queue = NULL;
static StaticQueue_t s_event_queue_buf;
static uint8_t s_event_queue_storage[EVENT_QUEUE_LEN * sizeof(button_event_t)];

static void send_button_event(button_event_t event)
{
    esp_err_t err = ble_button_tx_send_event(event);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to send button event %u: %s",
                 (unsigned)event, esp_err_to_name(err));
    }
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

static void event_worker_task(void *ctx)
{
    (void)ctx;

    button_event_t event;
    while (true) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        led_feedback_play_button_event(event);

        switch (event) {
            case BUTTON_EVENT_SINGLE_PRESS:
            case BUTTON_EVENT_DOUBLE_PRESS:
            case BUTTON_EVENT_TRIPLE_PRESS:
            case BUTTON_EVENT_LONG_PRESS:
                send_button_event(event);
                break;
            case BUTTON_EVENT_MAINTENANCE_HOLD:
                print_registration_credentials();
                break;
            default:
                break;
        }
    }
}

static void handle_button_event(button_event_t event, void *ctx)
{
    (void)ctx;

    if (!s_event_queue) {
        return;
    }

    BaseType_t queued = xQueueSend(s_event_queue, &event, 0);
    if (queued != pdTRUE) {
        ESP_LOGW(TAG, "dropping button event %u because runtime queue is full", (unsigned)event);
    }
}

void system_runtime_init(void)
{
    ESP_ERROR_CHECK(device_identity_init());
    ESP_ERROR_CHECK(ble_button_tx_init(device_identity_get_key()));
    led_feedback_init();

    s_event_queue = xQueueCreateStatic(EVENT_QUEUE_LEN,
                                       sizeof(button_event_t),
                                       s_event_queue_storage,
                                       &s_event_queue_buf);
    ESP_ERROR_CHECK(s_event_queue ? ESP_OK : ESP_ERR_NO_MEM);

    BaseType_t task_created = xTaskCreate(event_worker_task, "bb_runtime", 4096, NULL, 4, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(gpio_manager_init(handle_button_event, NULL));

    if (device_identity_key_was_created()) {
        print_registration_credentials();
    }

    ESP_LOGI(TAG, "BluButton v0 ready");
    ESP_LOGI(TAG, "Press BOOT for single/double/triple/long events");
    ESP_LOGI(TAG, "Hold BOOT for 10 seconds to reprint MAC and AES key");
}
