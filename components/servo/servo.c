#include "servo.h"
#include "driver/ledc.h"
#include "esp_check.h"

#define SERVO_TIMER     LEDC_TIMER_0
#define SERVO_CHANNEL   LEDC_CHANNEL_0
#define SERVO_MODE      LEDC_LOW_SPEED_MODE   
#define SERVO_RES_BITS  14
#define SERVO_FREQ_HZ   50

static const char *TAG = "servo";
static int s_min_us = 500, s_max_us = 2400;

esp_err_t servo_init(const servo_config_t *cfg)
{
    s_min_us = cfg->min_us;
    s_max_us = cfg->max_us;

    ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer");

    ledc_channel_config_t ch = {
        .gpio_num = cfg->gpio,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&ch);
}

void servo_write_angle(float deg)
{
    if (deg < 0.0f)   deg = 0.0f;
    if (deg > 180.0f) deg = 180.0f;
    int us = s_min_us + (int)((float)(s_max_us - s_min_us) * deg / 180.0f);
    uint32_t duty = (uint32_t)(((uint64_t)us * (1u << SERVO_RES_BITS)) / (1000000u / SERVO_FREQ_HZ));
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}
