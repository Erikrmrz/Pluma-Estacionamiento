#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mbedtls/sha256.h"

#include "config.h"
#include "rc522.h"
#include "servo.h"
#include "hcsr04.h"
#include "whitelist.h"

static const char *TAG = "pluma";

typedef enum { ST_CERRADA, ST_ABRIENDO, ST_ABIERTA, ST_CERRANDO } gate_state_t;
static const char *ST_NAME[] = { "CERRADA", "ABRIENDO", "ABIERTA", "CERRANDO" };

static QueueHandle_t q_tag;
static volatile bool s_vehicle;          
static volatile bool s_tamper;           
static volatile int  s_dist_cm = 999;
static float s_angle = SERVO_CLOSED_DEG;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* Interfaz de usuario */

static void leds(bool r, bool y, bool g)
{
    gpio_set_level(PIN_LED_RED, r);
    gpio_set_level(PIN_LED_YELLOW, y);
    gpio_set_level(PIN_LED_GREEN, g);
}

static void beep(int ms)
{
    gpio_set_level(PIN_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(PIN_BUZZER, 0);
}

static void beep_n(int n, int on_ms, int off_ms)
{
    for (int i = 0; i < n; i++) {
        beep(on_ms);
        if (i < n - 1) vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

/* Trazabilidad */

static void cred_id(const rc522_uid_t *u, char out[9])
{
    uint8_t buf[sizeof(CRED_SALT) - 1 + 7];
    size_t sl = sizeof(CRED_SALT) - 1;
    memcpy(buf, CRED_SALT, sl);
    memcpy(buf + sl, u->bytes, u->len);

    uint8_t h[32];
    mbedtls_sha256(buf, sl + u->len, h, 0);
    snprintf(out, 9, "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
}

static void log_event(const char *disposition, const rc522_uid_t *u)
{
    char id[9] = "--------";
    if (u) cred_id(u, id);
    printf("EVT {\"type\":\"ObjectEvent\",\"t_ms\":%" PRId64 ",\"action\":\"OBSERVE\","
           "\"epc\":\"urn:uni:cred:%s\",\"bizStep\":\"entering_exiting\","
           "\"disposition\":\"%s\",\"readPoint\":\"" READ_POINT "\",\"node\":\"N2\"}\n",
           now_ms(), id, disposition);
}

static void log_alert(const char *code)
{
    printf("ALERT {\"t_ms\":%" PRId64 ",\"code\":\"%s\",\"dist_cm\":%d,\"readPoint\":\""
           READ_POINT "\"}\n", now_ms(), code, s_dist_cm);
}

/* Movimiento de la pluma */

static void open_gate(void)
{
    while (s_angle < SERVO_OPEN_DEG) {
        s_angle += SERVO_STEP_DEG;
        if (s_angle > SERVO_OPEN_DEG) s_angle = SERVO_OPEN_DEG;
        servo_write_angle(s_angle);
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_MS));
    }
}

/* false para abrir si hay algo */

static bool close_gate_safe(void)
{
    while (s_angle > SERVO_CLOSED_DEG) {
        if (s_vehicle) return false;
        s_angle -= SERVO_STEP_DEG;
        if (s_angle < SERVO_CLOSED_DEG) s_angle = SERVO_CLOSED_DEG;
        servo_write_angle(s_angle);
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_MS));
    }
    return true;
}

/* Tareas */

static void rfid_task(void *arg)
{
    rc522_uid_t last = { .len = 0 };
    int64_t last_t = -100000;

    for (;;) {
        rc522_uid_t uid;
        if (rc522_read_uid(&uid) == ESP_OK) {
            int64_t t = now_ms();
            bool same = (uid.len == last.len) && memcmp(uid.bytes, last.bytes, uid.len) == 0;
            if (!(same && (t - last_t) < TAG_DEBOUNCE_MS)) {
                xQueueSend(q_tag, &uid, 0);
                last = uid;
                last_t = t;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RFID_POLL_MS));
    }
}

static int median3(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { int t = b; b = c; c = t; }
    if (a > b) { int t = a; a = b; b = t; }
    return b;
}

static int read_distance_cm(void)
{
    int d[3];
    for (int i = 0; i < 3; i++) {
        int v = hcsr04_read_cm();
        d[i] = (v < 0) ? 999 : v;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return median3(d[0], d[1], d[2]);
}

static void sensor_task(void *arg)
{
    int near_cnt = 0, far_cnt = 0;
    for (;;) {
        int dist = read_distance_cm();
        s_dist_cm = dist;

        if (dist < PRESENCE_CM) { near_cnt++; far_cnt = 0; }
        else                    { far_cnt++;  near_cnt = 0; }
        if (near_cnt >= PRESENCE_DEBOUNCE) s_vehicle = true;
        if (far_cnt  >= PRESENCE_DEBOUNCE) s_vehicle = false;

        s_tamper = (gpio_get_level(PIN_TILT) == TILT_ACTIVE_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

static void gate_task(void *arg)
{
    gate_state_t st = ST_CERRADA;
    int64_t t_state = now_ms();
    int64_t clear_since = 0;
    bool vehicle_seen = false, open_alerted = false;
    int denied = 0;
    int64_t denied_t0 = 0;
    int64_t enroll_until = 0;
    int64_t last_tamper = -100000;
    int btn_prev = 1;

    servo_write_angle(s_angle);
    leds(1, 0, 0);
    ESP_LOGI(TAG, "Estado: %s", ST_NAME[st]);

    for (;;) {
        rc522_uid_t uid;
        bool got = (xQueueReceive(q_tag, &uid, pdMS_TO_TICKS(40)) == pdTRUE);
        int64_t now = now_ms();

        int btn = gpio_get_level(PIN_BTN_ENROLL);
        bool btn_press = (btn_prev == 1 && btn == 0);
        btn_prev = btn;

        switch (st) {

        case ST_CERRADA:
            if (btn_press && !enroll_until) {
                enroll_until = now + ENROLL_WINDOW_MS;
                ESP_LOGI(TAG, "MODO REGISTRO %d s: acerca un tag (nuevo = agrega, existente = elimina)",
                         ENROLL_WINDOW_MS / 1000);
                beep(60);
            }

            if (enroll_until) {
                if (got) {
                    if (wl_contains(uid.bytes, uid.len)) {
                        wl_remove(uid.bytes, uid.len);
                        log_event("TAG_ELIMINADO", &uid);
                        beep(300);
                    } else if (wl_add(uid.bytes, uid.len) == ESP_OK) {
                        log_event("TAG_REGISTRADO", &uid);
                        beep_n(2, 80, 80);
                    } else {
                        ESP_LOGE(TAG, "No se pudo registrar (lista llena o error NVS)");
                        beep_n(3, 150, 80);
                    }
                    ESP_LOGI(TAG, "Credenciales registradas: %d", wl_count());
                    enroll_until = 0;
                    leds(1, 0, 0);
                } else if (now > enroll_until) {
                    ESP_LOGI(TAG, "Modo registro expirado");
                    enroll_until = 0;
                    leds(1, 0, 0);
                } else {
                    leds(0, (now / 250) % 2, 0);
                }
                break;
            }

            leds(1, 0, 0);

            if (got) {
                if (wl_contains(uid.bytes, uid.len)) {
                    log_event("ACCESO_CONCEDIDO", &uid);
                    denied = 0;
                    beep(80);
                    st = ST_ABRIENDO;
                    ESP_LOGI(TAG, "Estado: %s", ST_NAME[st]);
                } else {
                    log_event("ACCESO_DENEGADO", &uid);
                    if (now - denied_t0 > 60000) { denied = 0; denied_t0 = now; }
                    denied++;
                    beep_n(3, 80, 80);
                    if (denied >= 3) {
                        log_alert("INTENTOS_DENEGADOS_REPETIDOS");
                        denied = 0;
                    }
                }
            }

            if (s_tamper && (now - last_tamper) > 10000) {
                log_alert("PLUMA_FORZADA");
                last_tamper = now;
            }
            break;

        case ST_ABRIENDO:
            leds(0, 1, 0);
            open_gate();
            st = ST_ABIERTA;
            t_state = now_ms();
            vehicle_seen = false;
            open_alerted = false;
            clear_since = 0;
            leds(0, 0, 1);
            ESP_LOGI(TAG, "Estado: %s", ST_NAME[st]);
            break;

        case ST_ABIERTA:
            if (s_vehicle) {
                vehicle_seen = true;
                clear_since = 0;
            } else if (vehicle_seen) {
                if (clear_since == 0) clear_since = now;
                if (now - clear_since >= CLEAR_HOLD_MS) {
                    log_event("VEHICULO_CRUZO", NULL);
                    st = ST_CERRANDO;
                }
            }
            if (!vehicle_seen && (now - t_state) > OPEN_NO_VEHICLE_MS) {
                log_event("TIMEOUT_SIN_VEHICULO", NULL);
                st = ST_CERRANDO;
            }
            if (!open_alerted && (now - t_state) > OPEN_MAX_MS) {
                log_alert("PLUMA_ABIERTA_DEMASIADO_TIEMPO");
                open_alerted = true;
            }
            if (st == ST_CERRANDO) ESP_LOGI(TAG, "Estado: %s", ST_NAME[st]);
            break;

        case ST_CERRANDO:
            leds(0, 1, 0);
            if (close_gate_safe()) {
                st = ST_CERRADA;
                leds(1, 0, 0);
            } else {
                log_alert("OBSTACULO_DURANTE_CIERRE");
                st = ST_ABRIENDO;                     
            }
            ESP_LOGI(TAG, "Estado: %s", ST_NAME[st]);
            break;
        }
    }
}

/* Arranque */

static void gpio_setup(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LED_RED) | (1ULL << PIN_LED_GREEN) |
                        (1ULL << PIN_LED_YELLOW) | (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_BTN_ENROLL) | (1ULL << PIN_TILT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
    leds(0, 0, 0);
    gpio_set_level(PIN_BUZZER, 0);
}

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    gpio_setup();
    ESP_ERROR_CHECK(wl_init());

    servo_config_t sc = { .gpio = PIN_SERVO, .min_us = SERVO_MIN_US, .max_us = SERVO_MAX_US };
    ESP_ERROR_CHECK(servo_init(&sc));
    servo_write_angle(SERVO_CLOSED_DEG);

    ESP_ERROR_CHECK(hcsr04_init(PIN_HCSR04_TRIG, PIN_HCSR04_ECHO));

    rc522_config_t rc = {
        .host = SPI2_HOST,
        .sck = PIN_RC522_SCK, .mosi = PIN_RC522_MOSI, .miso = PIN_RC522_MISO,
        .cs = PIN_RC522_CS, .rst = PIN_RC522_RST,
        .clock_hz = 0,
    };
    if (rc522_init(&rc) != ESP_OK) {
        ESP_LOGE(TAG, "Fallo el RC522. Parpadeo rojo = revisa cableado.");
        for (;;) {
            gpio_set_level(PIN_LED_RED, 1); vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(PIN_LED_RED, 0); vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    q_tag = xQueueCreate(4, sizeof(rc522_uid_t));
    configASSERT(q_tag);

    ESP_LOGI(TAG, "=== Nodo de Acceso listo (%d credenciales) ===", wl_count());
    if (wl_count() == 0) {
        ESP_LOGW(TAG, "Lista vacia: pulsa el boton de registro (GPIO0) y acerca tu tarjeta/llavero");
    }
    beep_n(2, 60, 60);

    xTaskCreate(rfid_task,   "rfid",   4096, NULL, 4, NULL);
    xTaskCreate(sensor_task, "sensor", 3072, NULL, 4, NULL);
    xTaskCreate(gate_task,   "gate",   4096, NULL, 5, NULL);
}
