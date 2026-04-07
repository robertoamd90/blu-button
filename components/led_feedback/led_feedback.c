#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "board_config.h"
#include "led_feedback.h"

typedef enum {
    LED_PATTERN_IDLE = 0,
    LED_PATTERN_SINGLE,
    LED_PATTERN_DOUBLE,
    LED_PATTERN_TRIPLE,
    LED_PATTERN_LONG,
    LED_PATTERN_MAINTENANCE_HINT,
} led_pattern_t;

static bool s_initialized = false;
static TimerHandle_t s_led_timer = NULL;
static StaticTimer_t s_led_timer_buf;
static led_pattern_t s_led_pattern = LED_PATTERN_IDLE;
static uint8_t s_led_phase = 0;
static bool s_led_level = false;

static int led_level_for(bool on)
{
    return board_config_system_led_active_low() ? !on : on;
}

static void led_apply_level(bool on)
{
    s_led_level = on;
    gpio_set_level((gpio_num_t)board_config_system_led_gpio(), led_level_for(on));
}

static void led_set_pattern(led_pattern_t pattern)
{
    if (!s_led_timer) {
        return;
    }

    xTimerStop(s_led_timer, 0);
    s_led_pattern = pattern;
    s_led_phase = 0;
    led_apply_level(false);

    if (pattern != LED_PATTERN_IDLE) {
        xTimerChangePeriod(s_led_timer, pdMS_TO_TICKS(60), 0);
        xTimerStart(s_led_timer, 0);
    }
}

static void led_timer_cb(TimerHandle_t timer)
{
    TickType_t next = pdMS_TO_TICKS(120);

    switch (s_led_pattern) {
        case LED_PATTERN_SINGLE:
            if (s_led_phase == 0) {
                led_apply_level(true);
            } else if (s_led_phase == 1) {
                led_apply_level(false);
                s_led_pattern = LED_PATTERN_IDLE;
                xTimerStop(timer, 0);
                return;
            }
            break;

        case LED_PATTERN_DOUBLE:
        case LED_PATTERN_TRIPLE: {
            uint8_t blink_count = (s_led_pattern == LED_PATTERN_DOUBLE) ? 2 : 3;
            if (s_led_phase < blink_count * 2) {
                led_apply_level((s_led_phase % 2) == 0);
            } else {
                led_apply_level(false);
                s_led_pattern = LED_PATTERN_IDLE;
                xTimerStop(timer, 0);
                return;
            }
            break;
        }

        case LED_PATTERN_LONG:
            next = pdMS_TO_TICKS(450);
            if (s_led_phase == 0) {
                led_apply_level(true);
            } else if (s_led_phase == 1) {
                led_apply_level(false);
                s_led_pattern = LED_PATTERN_IDLE;
                xTimerStop(timer, 0);
                return;
            }
            break;

        case LED_PATTERN_MAINTENANCE_HINT:
            next = pdMS_TO_TICKS(90);
            led_apply_level(!s_led_level);
            break;

        case LED_PATTERN_IDLE:
        default:
            led_apply_level(false);
            xTimerStop(timer, 0);
            return;
    }

    s_led_phase++;
    xTimerChangePeriod(timer, next, 0);
}

void led_feedback_init(void)
{
    if (s_initialized) {
        return;
    }

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << board_config_system_led_gpio(),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    led_apply_level(false);

    s_led_timer = xTimerCreateStatic("bb_led",
                                     pdMS_TO_TICKS(120),
                                     pdFALSE,
                                     NULL,
                                     led_timer_cb,
                                     &s_led_timer_buf);
    s_initialized = true;
}

void led_feedback_play_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_SINGLE_PRESS:
            led_set_pattern(LED_PATTERN_SINGLE);
            break;
        case BUTTON_EVENT_DOUBLE_PRESS:
            led_set_pattern(LED_PATTERN_DOUBLE);
            break;
        case BUTTON_EVENT_TRIPLE_PRESS:
            led_set_pattern(LED_PATTERN_TRIPLE);
            break;
        case BUTTON_EVENT_LONG_PRESS:
            led_set_pattern(LED_PATTERN_LONG);
            break;
        case BUTTON_EVENT_MAINTENANCE_HOLD:
            led_set_pattern(LED_PATTERN_MAINTENANCE_HINT);
            break;
        default:
            led_set_pattern(LED_PATTERN_IDLE);
            break;
    }
}
