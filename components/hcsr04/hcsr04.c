#include "hcsr04.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_check.h"

static const char *TAG = "hcsr04";
static gpio_num_t s_trig, s_echo;

esp_err_t hcsr04_init(gpio_num_t trig, gpio_num_t echo)
{
    s_trig = trig;
    s_echo = echo;

    gpio_config_t out = { .pin_bit_mask = 1ULL << trig, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "trig");
    gpio_set_level(trig, 0);

    gpio_config_t in = { .pin_bit_mask = 1ULL << echo, .mode = GPIO_MODE_INPUT };
    return gpio_config(&in);
}

int hcsr04_read_cm(void)
{
    gpio_set_level(s_trig, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_trig, 1);
    esp_rom_delay_us(10);
    gpio_set_level(s_trig, 0);

    int64_t t0 = esp_timer_get_time();
    while (!gpio_get_level(s_echo)) {
        if (esp_timer_get_time() - t0 > 6000) return -1;
    }
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_echo)) {
        if (esp_timer_get_time() - start > 25000) return -1;
    }
    int64_t dur_us = esp_timer_get_time() - start;
    return (int)(dur_us / 58);      
}
