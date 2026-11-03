#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WL_MAX      32
#define WL_UID_MAX  7

esp_err_t wl_init(void);
bool      wl_contains(const uint8_t *uid, uint8_t len);
esp_err_t wl_add(const uint8_t *uid, uint8_t len);     
esp_err_t wl_remove(const uint8_t *uid, uint8_t len);   
int       wl_count(void);
