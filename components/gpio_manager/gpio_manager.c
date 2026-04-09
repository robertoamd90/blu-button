#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/soc_caps.h"
#include "board_config.h"
#include "gpio_manager.h"

static const char *TAG = "gpio_manager";

#define BUTTON_DEBOUNCE_MS        15
#define BUTTON_MULTI_CLICK_MS    450
#define BUTTON_LONG_PRESS_MS     700
#define BUTTON_MAINTENANCE_MS  10000
#define BUTTON_EDGE_QUEUE_LEN     16

static bool s_button_level_ready = false;
static bool s_isr_service_installed = false;
static bool s_button_edge_capture_ready = false;
static bool s_button_active_low = true;
static QueueHandle_t s_button_edge_queue = NULL;
static volatile bool s_button_edge_overflow = false;

typedef struct {
    bool pressed;
    uint32_t changed_ms;
} button_edge_event_t;

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    uint32_t raw_changed_ms;
} button_tracker_t;

typedef struct {
    button_tracker_t tracker;
    bool press_in_progress;
    uint32_t press_started_ms;
    uint8_t short_press_count;
    uint32_t multi_click_deadline_ms;
} gesture_session_t;

typedef struct {
    bool armed;
    bool implicit_initial_press;
    bool have_queued_edges;
    bool sampled_pressed;
    uint32_t now_ms;
} wake_capture_state_t;

static TickType_t timeout_ms_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }

    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return (ticks > 0) ? ticks : 1;
}

static void IRAM_ATTR button_gpio_isr_handler(void *arg)
{
    const uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    const int level = gpio_ll_get_level(&GPIO, gpio_num);
    button_edge_event_t event = {
        .pressed = s_button_active_low ? (level == 0) : (level != 0),
        .changed_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    BaseType_t high_task_wakeup = pdFALSE;

    if (s_button_edge_queue) {
        if (xQueueSendFromISR(s_button_edge_queue, &event, &high_task_wakeup) != pdTRUE) {
            s_button_edge_overflow = true;
        }
    }

    if (high_task_wakeup == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t configure_button_level_input(void)
{
    const gpio_num_t button_gpio = (gpio_num_t)board_config_boot_button_gpio();

    if (s_button_level_ready) {
        return ESP_OK;
    }

    s_button_active_low = board_config_boot_button_active_low();

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << button_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = s_button_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = s_button_active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&btn_cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_button_level_ready = true;
    return ESP_OK;
}

static esp_err_t configure_button_edge_capture(void)
{
    const gpio_num_t button_gpio = (gpio_num_t)board_config_boot_button_gpio();
    esp_err_t err = configure_button_level_input();
    if (err != ESP_OK) {
        return err;
    }

    if (s_button_edge_capture_ready) {
        return ESP_OK;
    }

    if (!s_button_edge_queue) {
        s_button_edge_queue = xQueueCreate(BUTTON_EDGE_QUEUE_LEN, sizeof(button_edge_event_t));
        if (!s_button_edge_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_isr_service_installed) {
        err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_isr_service_installed = true;
    }

    err = gpio_set_intr_type(button_gpio, GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_add(button_gpio, button_gpio_isr_handler, (void *)(uintptr_t)button_gpio);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_button_edge_capture_ready = true;
    return ESP_OK;
}

bool gpio_manager_boot_button_pressed(void)
{
    if (configure_button_level_input() != ESP_OK) {
        return false;
    }
    int level = gpio_get_level((gpio_num_t)board_config_boot_button_gpio());
    return s_button_active_low ? (level == 0) : (level != 0);
}

static uint32_t current_boot_elapsed_ms(void)
{
    int64_t elapsed_us = esp_timer_get_time();
    if (elapsed_us <= 0) {
        return 0;
    }

    return (uint32_t)(elapsed_us / 1000);
}

static void button_tracker_init(button_tracker_t *tracker, bool pressed, uint32_t now_ms)
{
    if (!tracker) {
        return;
    }

    tracker->raw_pressed = pressed;
    tracker->stable_pressed = pressed;
    tracker->raw_changed_ms = now_ms;
}

static void button_tracker_note_raw_state(button_tracker_t *tracker,
                                          bool pressed,
                                          uint32_t now_ms)
{
    if (!tracker) {
        return;
    }

    if (tracker->raw_pressed != pressed) {
        tracker->raw_pressed = pressed;
        tracker->raw_changed_ms = now_ms;
    }
}

static void button_tracker_finalize_debounce(button_tracker_t *tracker,
                                             uint32_t now_ms,
                                             bool *out_press_edge,
                                             bool *out_release_edge)
{
    if (!tracker || !out_press_edge || !out_release_edge) {
        return;
    }

    *out_press_edge = false;
    *out_release_edge = false;

    if (tracker->stable_pressed == tracker->raw_pressed) {
        return;
    }

    if ((now_ms - tracker->raw_changed_ms) < BUTTON_DEBOUNCE_MS) {
        return;
    }

    tracker->stable_pressed = tracker->raw_pressed;

    if (tracker->stable_pressed) {
        *out_press_edge = true;
    } else {
        *out_release_edge = true;
    }
}

static button_event_t classify_short_press_count(uint8_t short_press_count)
{
    if (short_press_count >= 3) {
        return BUTTON_EVENT_TRIPLE_PRESS;
    }
    if (short_press_count == 2) {
        return BUTTON_EVENT_DOUBLE_PRESS;
    }
    return BUTTON_EVENT_SINGLE_PRESS;
}

static void gesture_session_init(gesture_session_t *session, bool pressed, uint32_t now_ms)
{
    if (!session) {
        return;
    }

    button_tracker_init(&session->tracker, pressed, now_ms);
    session->press_in_progress = false;
    session->press_started_ms = 0;
    session->short_press_count = 0;
    session->multi_click_deadline_ms = 0;
}

static void gesture_session_note_raw_state(gesture_session_t *session,
                                           bool pressed,
                                           uint32_t now_ms)
{
    if (!session) {
        return;
    }

    button_tracker_note_raw_state(&session->tracker, pressed, now_ms);
}

static bool gesture_session_process_edges(gesture_session_t *session,
                                          bool press_edge,
                                          bool release_edge,
                                          button_event_t *out_event,
                                          bool *out_have_event)
{
    if (press_edge && !session->press_in_progress) {
        session->press_in_progress = true;
        session->press_started_ms = session->tracker.raw_changed_ms;
    }

    if (!release_edge || !session->press_in_progress) {
        return false;
    }

    uint32_t held_ms = session->tracker.raw_changed_ms - session->press_started_ms;

    session->press_in_progress = false;
    if (held_ms >= BUTTON_MAINTENANCE_MS) {
        ESP_LOGI(TAG, "maintenance hold detected (%" PRIu32 " ms)", held_ms);
        *out_event = BUTTON_EVENT_MAINTENANCE_HOLD;
        *out_have_event = true;
        return true;
    }

    if (held_ms >= BUTTON_LONG_PRESS_MS) {
        *out_event = BUTTON_EVENT_LONG_PRESS;
        *out_have_event = true;
        return true;
    }

    if (session->short_press_count < 3) {
        session->short_press_count++;
    }
    session->multi_click_deadline_ms = session->tracker.raw_changed_ms + BUTTON_MULTI_CLICK_MS;
    return false;
}

static bool gesture_session_finalize(gesture_session_t *session,
                                     uint32_t now_ms,
                                     button_event_t *out_event,
                                     bool *out_have_event)
{
    bool press_edge = false;
    bool release_edge = false;

    button_tracker_finalize_debounce(&session->tracker, now_ms, &press_edge, &release_edge);
    return gesture_session_process_edges(session, press_edge, release_edge, out_event, out_have_event);
}

static bool gesture_session_finalize_click_timeout(gesture_session_t *session,
                                                   uint32_t now_ms,
                                                   button_event_t *out_event,
                                                   bool *out_have_event)
{
    if (!session->press_in_progress
        && session->short_press_count > 0
        && now_ms >= session->multi_click_deadline_ms
        && !session->tracker.raw_pressed) {
        *out_event = classify_short_press_count(session->short_press_count);
        *out_have_event = true;
        return true;
    }

    return false;
}

static bool gesture_session_apply_queued_edge(gesture_session_t *session,
                                              const button_edge_event_t *edge_event,
                                              button_event_t *out_event,
                                              bool *out_have_event)
{
    bool press_edge = false;
    bool release_edge = false;

    button_tracker_finalize_debounce(&session->tracker, edge_event->changed_ms, &press_edge, &release_edge);
    if (gesture_session_process_edges(session, press_edge, release_edge, out_event, out_have_event)) {
        return true;
    }

    gesture_session_note_raw_state(session, edge_event->pressed, edge_event->changed_ms);
    return false;
}

static bool woke_from_boot_button(uint32_t wakeup_causes)
{
    return (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) != 0
        || (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) != 0
        || (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) != 0;
}

static bool has_wake_session(uint32_t wakeup_causes)
{
    return gpio_manager_boot_button_pressed() || woke_from_boot_button(wakeup_causes);
}

esp_err_t gpio_manager_begin_wake_capture(gpio_wake_capture_t *capture,
                                          uint32_t wakeup_causes)
{
    if (!capture) {
        return ESP_ERR_INVALID_ARG;
    }

    capture->wakeup_causes = wakeup_causes;
    capture->armed = has_wake_session(wakeup_causes);
    if (!capture->armed) {
        return ESP_OK;
    }

    return configure_button_edge_capture();
}

static void build_wake_capture_state(const gpio_wake_capture_t *capture,
                                     wake_capture_state_t *state)
{
    if (!capture || !state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->armed = capture->armed;
    state->implicit_initial_press = woke_from_boot_button(capture->wakeup_causes);
    state->have_queued_edges = s_button_edge_queue && uxQueueMessagesWaiting(s_button_edge_queue) > 0;
    state->now_ms = current_boot_elapsed_ms();
    state->sampled_pressed = gpio_manager_boot_button_pressed();
}

static void gesture_session_apply_initial_wake_state(gesture_session_t *session,
                                                     const wake_capture_state_t *state)
{
    if (!session || !state) {
        return;
    }

    if (state->implicit_initial_press && state->sampled_pressed) {
        session->tracker.raw_pressed = true;
        session->tracker.stable_pressed = true;
        session->tracker.raw_changed_ms = 0;
        session->press_in_progress = true;
        session->press_started_ms = 0;
        return;
    }

    if (state->implicit_initial_press) {
        session->short_press_count = 1;
        session->multi_click_deadline_ms = BUTTON_MULTI_CLICK_MS;
        return;
    }

    if (state->sampled_pressed) {
        session->press_in_progress = true;
        session->press_started_ms = session->tracker.raw_changed_ms;
    }
}

esp_err_t gpio_manager_finish_wake_capture(const gpio_wake_capture_t *capture,
                                           button_event_t *out_event,
                                           bool *out_have_event)
{
    button_edge_event_t edge_event;
    uint32_t now_ms;
    wake_capture_state_t state;

    if (!capture || !out_event || !out_have_event) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_have_event = false;
    *out_event = BUTTON_EVENT_SINGLE_PRESS;

    if (!capture->armed) {
        return ESP_OK;
    }

    build_wake_capture_state(capture, &state);
    gesture_session_t gesture_session;
    gesture_session_init(&gesture_session, state.sampled_pressed, state.now_ms);
    gesture_session_apply_initial_wake_state(&gesture_session, &state);
    while (gesture_session.press_in_progress || gesture_session.short_press_count > 0) {
        uint32_t wait_ms = UINT32_MAX;
        TickType_t wait_ticks;
        BaseType_t got_edge;

        now_ms = current_boot_elapsed_ms();

        if (gesture_session.tracker.stable_pressed != gesture_session.tracker.raw_pressed) {
            const uint32_t debounce_deadline_ms = gesture_session.tracker.raw_changed_ms + BUTTON_DEBOUNCE_MS;

            wait_ms = (debounce_deadline_ms > now_ms) ? (debounce_deadline_ms - now_ms) : 0;
        }

        if (!gesture_session.press_in_progress && gesture_session.short_press_count > 0) {
            const uint32_t multi_click_wait_ms =
                (gesture_session.multi_click_deadline_ms > now_ms) ? (gesture_session.multi_click_deadline_ms - now_ms) : 0;

            if (multi_click_wait_ms < wait_ms) {
                wait_ms = multi_click_wait_ms;
            }
        }

        wait_ticks = (wait_ms == UINT32_MAX) ? portMAX_DELAY : timeout_ms_to_ticks(wait_ms);
        got_edge = xQueueReceive(s_button_edge_queue, &edge_event, wait_ticks);
        now_ms = current_boot_elapsed_ms();

        if (s_button_edge_overflow) {
            while (xQueueReceive(s_button_edge_queue, &edge_event, 0) == pdTRUE) {
            }
            gesture_session_note_raw_state(&gesture_session, gpio_manager_boot_button_pressed(), now_ms);
            s_button_edge_overflow = false;
        } else if (got_edge == pdTRUE) {
            do {
                if (gesture_session_apply_queued_edge(&gesture_session, &edge_event, out_event, out_have_event)) {
                    return ESP_OK;
                }
            } while (xQueueReceive(s_button_edge_queue, &edge_event, 0) == pdTRUE);
        } else {
            gesture_session_note_raw_state(&gesture_session, gpio_manager_boot_button_pressed(), now_ms);
        }

        if (gesture_session_finalize(&gesture_session, now_ms, out_event, out_have_event)) {
            return ESP_OK;
        }

        if (gesture_session_finalize_click_timeout(&gesture_session, now_ms, out_event, out_have_event)) {
            return ESP_OK;
        }
    }

    return ESP_OK;
}

esp_err_t gpio_manager_enable_boot_button_wakeup(void)
{
    const gpio_num_t wake_gpio = (gpio_num_t)board_config_boot_button_gpio();
    const bool active_low = board_config_boot_button_active_low();
    const int wake_level = active_low ? 0 : 1;

    esp_err_t err = configure_button_level_input();
    if (err != ESP_OK) {
        return err;
    }

    if (!esp_sleep_is_valid_wakeup_gpio(wake_gpio)) {
        ESP_LOGE(TAG, "GPIO %d is not a valid deep-sleep wake source", (int)wake_gpio);
        return ESP_ERR_INVALID_ARG;
    }

#if SOC_PM_SUPPORT_EXT1_WAKEUP
#if CONFIG_IDF_TARGET_ESP32
    esp_sleep_ext1_wakeup_mode_t mode = active_low ? ESP_EXT1_WAKEUP_ALL_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;
#else
    esp_sleep_ext1_wakeup_mode_t mode = active_low ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;
#endif
    err = esp_sleep_enable_ext1_wakeup_io(1ULL << wake_gpio, mode);
#elif SOC_GPIO_SUPPORT_HP_PERIPH_PD_SLEEP_WAKEUP
    esp_sleep_gpio_wake_up_mode_t mode = active_low ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH;
    err = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << wake_gpio, mode);
#elif SOC_PM_SUPPORT_EXT0_WAKEUP
    err = esp_sleep_enable_ext0_wakeup(wake_gpio, active_low ? 0 : 1);
#else
#error "No supported deep-sleep wake source available for boot button"
#endif

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configured deep-sleep wake on GPIO %d level=%d", (int)wake_gpio, wake_level);
    }

    return err;
}
