#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bytes[7];
    uint8_t len;
} pn532_uid_t;

typedef struct {
    spi_host_device_t host;  
    gpio_num_t sck, mosi, miso, cs, rst;
    int clock_hz;             
} pn532_config_t;

esp_err_t pn532_init(const pn532_config_t *cfg);

uint32_t pn532_firmware(void);

esp_err_t pn532_read_uid(pn532_uid_t *uid);

#ifdef __cplusplus
}
#endif
