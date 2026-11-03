#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

typedef struct {
    gpio_num_t gpio;
    int min_us;     
    int max_us;     
} servo_config_t;

esp_err_t servo_init(const servo_config_t *cfg);
void servo_write_angle(float deg);  
