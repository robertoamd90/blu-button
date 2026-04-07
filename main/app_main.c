#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "system_runtime.h"

static const char *TAG = "blu_button";

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

void app_main(void)
{
    init_nvs_or_die();

    ESP_LOGI(TAG, "BluButton booting");
    system_runtime_init();
}
