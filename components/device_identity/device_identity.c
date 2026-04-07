#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "device_identity.h"

static const char *TAG = "device_identity";
static const char *NVS_NS = "identity";
static const char *NVS_KEY_AES = "aes_key";

static uint8_t s_key[16];
static uint8_t s_mac[6];
static bool s_initialized = false;
static bool s_key_created = false;

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = HEX[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = HEX[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

esp_err_t device_identity_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_read_mac(s_mac, ESP_MAC_BT);
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t key_len = sizeof(s_key);
    err = nvs_get_blob(handle, NVS_KEY_AES, s_key, &key_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(s_key, sizeof(s_key));
        err = nvs_set_blob(handle, NVS_KEY_AES, s_key, sizeof(s_key));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        if (err != ESP_OK) {
            memset(s_key, 0, sizeof(s_key));
            nvs_close(handle);
            return err;
        }
        s_key_created = true;
        ESP_LOGI(TAG, "Generated new device AES key");
    } else if (err == ESP_OK && key_len != sizeof(s_key)) {
        memset(s_key, 0, sizeof(s_key));
        nvs_close(handle);
        ESP_LOGE(TAG, "Stored AES key has invalid length: %u", (unsigned)key_len);
        return ESP_ERR_INVALID_SIZE;
    } else if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    s_initialized = true;
    return ESP_OK;
}

const uint8_t *device_identity_get_key(void)
{
    return s_key;
}

esp_err_t device_identity_get_mac(uint8_t out_mac[6])
{
    if (!s_initialized || !out_mac) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(out_mac, s_mac, sizeof(s_mac));
    return ESP_OK;
}

bool device_identity_key_was_created(void)
{
    return s_key_created;
}

void device_identity_format_key_hex(char out[33])
{
    bytes_to_hex(s_key, sizeof(s_key), out);
}
