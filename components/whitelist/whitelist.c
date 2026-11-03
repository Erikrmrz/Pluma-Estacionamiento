#include "whitelist.h"
#include <string.h>
#include "nvs.h"
#include "esp_log.h"

#define NS        "pluma"
#define KEY_LIST  "wl"
#define KEY_CNT   "wl_cnt"

typedef struct {
    uint8_t len;
    uint8_t uid[WL_UID_MAX];
} entry_t;

static const char *TAG = "whitelist";
static entry_t s_list[WL_MAX];
static uint8_t s_count;

static esp_err_t save(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY_LIST, s_list, sizeof s_list);
    if (e == ESP_OK) e = nvs_set_u8(h, KEY_CNT, s_count);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t wl_init(void)
{
    memset(s_list, 0, sizeof s_list);
    s_count = 0;

    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;

    size_t size = sizeof s_list;
    uint8_t cnt = 0;
    if (nvs_get_u8(h, KEY_CNT, &cnt) == ESP_OK &&
        nvs_get_blob(h, KEY_LIST, s_list, &size) == ESP_OK &&
        size == sizeof s_list && cnt <= WL_MAX) {
        s_count = cnt;
    } else {
        memset(s_list, 0, sizeof s_list);
        s_count = 0;
        ESP_LOGW(TAG, "Lista blanca vacia (primer arranque)");
    }
    nvs_close(h);
    ESP_LOGI(TAG, "%u credenciales registradas", s_count);
    return ESP_OK;
}

static int find(const uint8_t *uid, uint8_t len)
{
    for (int i = 0; i < s_count; i++) {
        if (s_list[i].len == len && memcmp(s_list[i].uid, uid, len) == 0) return i;
    }
    return -1;
}

bool wl_contains(const uint8_t *uid, uint8_t len) { return find(uid, len) >= 0; }

esp_err_t wl_add(const uint8_t *uid, uint8_t len)
{
    if (len == 0 || len > WL_UID_MAX) return ESP_ERR_INVALID_ARG;
    if (find(uid, len) >= 0) return ESP_OK;
    if (s_count >= WL_MAX) return ESP_ERR_NO_MEM;
    s_list[s_count].len = len;
    memcpy(s_list[s_count].uid, uid, len);
    s_count++;
    return save();
}

esp_err_t wl_remove(const uint8_t *uid, uint8_t len)
{
    int i = find(uid, len);
    if (i < 0) return ESP_ERR_NOT_FOUND;
    s_list[i] = s_list[s_count - 1];
    memset(&s_list[s_count - 1], 0, sizeof(entry_t));
    s_count--;
    return save();
}

int wl_count(void) { return s_count; }
