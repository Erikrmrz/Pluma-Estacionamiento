#pragma once
#include "driver/gpio.h"

/* =====================================================================
 *  PINES  (ESP32-C6-DevKitC-1) - verifica contra el pinout de TU placa.
 *  Se evitan pines de arranque (4,5,8,9,15), USB (12,13) y consola (16,17).
 * ===================================================================== */
#define PIN_RC522_SCK     GPIO_NUM_6
#define PIN_RC522_MOSI    GPIO_NUM_7
#define PIN_RC522_MISO    GPIO_NUM_2
#define PIN_RC522_CS      GPIO_NUM_10   /* "SDA" en el modulo */
#define PIN_RC522_RST     GPIO_NUM_11

#define PIN_SERVO         GPIO_NUM_18
#define PIN_HCSR04_TRIG   GPIO_NUM_19
#define PIN_HCSR04_ECHO   GPIO_NUM_20   /* OJO: con divisor de voltaje (5V -> 3.3V) */

#define PIN_LED_RED       GPIO_NUM_22
#define PIN_LED_GREEN     GPIO_NUM_21
#define PIN_LED_YELLOW    GPIO_NUM_3
#define PIN_BUZZER        GPIO_NUM_23   /* buzzer ACTIVO */

#define PIN_BTN_ENROLL    GPIO_NUM_0    /* boton a GND, pull-up interno */
#define PIN_TILT          GPIO_NUM_1    /* tilt switch a GND, pull-up interno */
#define TILT_ACTIVE_LEVEL 0             /* 0 = contacto cerrado a GND = inclinado */

/* =====================================================================
 *  PARAMETROS DE LA PLUMA
 * ===================================================================== */
#define SERVO_MIN_US      500           /* pulso para 0 grados   (SG90 ~500)  */
#define SERVO_MAX_US      2400          /* pulso para 180 grados (SG90 ~2400) */
#define SERVO_CLOSED_DEG  0.0f          /* invierte los valores si gira al reves */
#define SERVO_OPEN_DEG    90.0f
#define SERVO_STEP_DEG    2.0f
#define SERVO_STEP_MS     15

#define PRESENCE_CM       30            /* objeto a menos de esto = vehiculo presente */
#define PRESENCE_DEBOUNCE 2             /* lecturas consecutivas para cambiar de estado */
#define SENSOR_PERIOD_MS  30

#define CLEAR_HOLD_MS         1500      /* zona libre este tiempo -> cerrar */
#define OPEN_NO_VEHICLE_MS    10000     /* abierta sin que pase nadie -> cerrar */
#define OPEN_MAX_MS           60000     /* abierta con vehiculo detenido -> ALERTA */

#define RFID_POLL_MS      120
#define TAG_DEBOUNCE_MS   2500          /* ignora el mismo tag repetido */
#define ENROLL_WINDOW_MS  10000

/* Sal para seudonimizar el UID en los eventos (no sale el UID real) */
#define CRED_SALT         "uni-pluma-v1"
#define READ_POINT        "urn:uni:loc:estac-norte:pluma-01"
