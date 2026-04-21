#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "board_config.h"
#include "led_feedback.h"

typedef struct {
    uint32_t start_delay_ms;
    uint32_t on_ms;
    uint32_t off_ms;
    uint8_t pulse_count;
} led_pattern_t;

static bool s_initialized = false;
static bool s_led_available = false;

#define LED_PATTERN_START_MS       60
#define LED_PATTERN_STEP_MS       120
#define LED_PATTERN_LONG_STEP_MS  450

static int led_level_for(bool on)
{
    return board_config_system_led_active_low() ? !on : on;
}

static void led_apply_level(bool on)
{
    gpio_set_level((gpio_num_t)board_config_system_led_gpio(), led_level_for(on));
}

static bool led_is_available(void)
{
    return board_config_system_led_gpio() >= 0;
}

static void led_delay_ms(uint32_t delay_ms)
{
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static bool led_pattern_for_event(button_event_t event, led_pattern_t *out_pattern)
{
    if (!out_pattern) {
        return false;
    }

    switch (event) {
        case BUTTON_EVENT_SINGLE_PRESS:
            *out_pattern = (led_pattern_t) {
                .start_delay_ms = LED_PATTERN_START_MS,
                .on_ms = LED_PATTERN_STEP_MS,
                .off_ms = LED_PATTERN_STEP_MS,
                .pulse_count = 1,
            };
            return true;
        case BUTTON_EVENT_DOUBLE_PRESS:
            *out_pattern = (led_pattern_t) {
                .start_delay_ms = LED_PATTERN_START_MS,
                .on_ms = LED_PATTERN_STEP_MS,
                .off_ms = LED_PATTERN_STEP_MS,
                .pulse_count = 2,
            };
            return true;
        case BUTTON_EVENT_TRIPLE_PRESS:
            *out_pattern = (led_pattern_t) {
                .start_delay_ms = LED_PATTERN_START_MS,
                .on_ms = LED_PATTERN_STEP_MS,
                .off_ms = LED_PATTERN_STEP_MS,
                .pulse_count = 3,
            };
            return true;
        case BUTTON_EVENT_LONG_PRESS:
            *out_pattern = (led_pattern_t) {
                .start_delay_ms = LED_PATTERN_START_MS,
                .on_ms = LED_PATTERN_LONG_STEP_MS,
                .off_ms = 0,
                .pulse_count = 1,
            };
            return true;
        default:
            return false;
    }
}

static void led_run_pattern(const led_pattern_t *pattern)
{
    if (!pattern || pattern->pulse_count == 0) {
        led_apply_level(false);
        return;
    }

    led_apply_level(false);
    led_delay_ms(pattern->start_delay_ms);

    for (uint8_t i = 0; i < pattern->pulse_count; ++i) {
        led_apply_level(true);
        led_delay_ms(pattern->on_ms);
        led_apply_level(false);
        if ((i + 1U) < pattern->pulse_count) {
            led_delay_ms(pattern->off_ms);
        }
    }
}

esp_err_t led_feedback_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!led_is_available()) {
        s_initialized = true;
        s_led_available = false;
        return ESP_OK;
    }

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << board_config_system_led_gpio(),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led_cfg);
    if (err != ESP_OK) {
        return err;
    }
    led_apply_level(false);
    s_led_available = true;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t led_feedback_run_button_event_pattern_blocking(button_event_t event)
{
    led_pattern_t pattern;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_led_available) {
        return ESP_OK;
    }

    if (!led_pattern_for_event(event, &pattern)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    led_run_pattern(&pattern);
    return ESP_OK;
}
