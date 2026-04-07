#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "board_config.h"
#include "gpio_manager.h"

static const char *TAG = "gpio_manager";

#define BUTTON_POLL_MS             25
#define BUTTON_MULTI_CLICK_MS      350
#define BUTTON_LONG_PRESS_MS       700
#define BUTTON_MAINTENANCE_MS    10000

static bool s_initialized = false;
static gpio_manager_button_event_cb_t s_button_cb = NULL;
static void *s_button_cb_ctx = NULL;

static bool boot_button_pressed(void)
{
    int level = gpio_get_level((gpio_num_t)board_config_boot_button_gpio());
    return board_config_boot_button_active_low() ? (level == 0) : (level != 0);
}

static void emit_button_event(button_event_t event)
{
    if (s_button_cb) {
        s_button_cb(event, s_button_cb_ctx);
    }
}

static void button_task(void *arg)
{
    (void)arg;

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << board_config_boot_button_gpio(),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = board_config_boot_button_active_low() ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = board_config_boot_button_active_low() ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    uint8_t short_press_count = 0;
    uint32_t pending_gap_ms = 0;

    while (true) {
        if (!boot_button_pressed()) {
            if (short_press_count > 0) {
                pending_gap_ms += BUTTON_POLL_MS;
                if (pending_gap_ms >= BUTTON_MULTI_CLICK_MS) {
                    button_event_t event = BUTTON_EVENT_SINGLE_PRESS;
                    if (short_press_count == 2) {
                        event = BUTTON_EVENT_DOUBLE_PRESS;
                    } else if (short_press_count >= 3) {
                        event = BUTTON_EVENT_TRIPLE_PRESS;
                    }
                    emit_button_event(event);
                    short_press_count = 0;
                    pending_gap_ms = 0;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
            continue;
        }

        uint32_t held_ms = 0;
        while (boot_button_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
            held_ms += BUTTON_POLL_MS;
        }

        if (held_ms >= BUTTON_MAINTENANCE_MS) {
            short_press_count = 0;
            pending_gap_ms = 0;
            ESP_LOGI(TAG, "maintenance hold detected (%" PRIu32 " ms)", held_ms);
            emit_button_event(BUTTON_EVENT_MAINTENANCE_HOLD);
        } else if (held_ms >= BUTTON_LONG_PRESS_MS) {
            short_press_count = 0;
            pending_gap_ms = 0;
            emit_button_event(BUTTON_EVENT_LONG_PRESS);
        } else {
            if (short_press_count < 3) {
                short_press_count++;
            }
            pending_gap_ms = 0;
        }
    }
}

esp_err_t gpio_manager_init(gpio_manager_button_event_cb_t cb, void *ctx)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }

    s_button_cb = cb;
    s_button_cb_ctx = ctx;

    BaseType_t task_created = xTaskCreate(button_task, "bb_button", 4096, NULL, 4, NULL);
    if (task_created != pdPASS) {
        s_button_cb = NULL;
        s_button_cb_ctx = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}
