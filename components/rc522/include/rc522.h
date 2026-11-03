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
} rc522_uid_t;

typedef struct {
    spi_host_device_t host;   
    gpio_num_t sck, mosi, miso, cs, rst;
    int clock_hz;            
} rc522_config_t;

esp_err_t rc522_init(const rc522_config_t *cfg);

uint8_t rc522_version(void);


esp_err_t rc522_read_uid(rc522_uid_t *uid);

#ifdef __cplusplus
}
#endif
