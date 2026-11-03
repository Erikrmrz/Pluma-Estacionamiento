#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t hcsr04_init(gpio_num_t trig, gpio_num_t echo);

int hcsr04_read_cm(void);
