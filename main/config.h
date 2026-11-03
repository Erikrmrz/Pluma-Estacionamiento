#pragma once
#include "driver/gpio.h"


#define PIN_RC522_SCK     GPIO_NUM_6
#define PIN_RC522_MOSI    GPIO_NUM_7
#define PIN_RC522_MISO    GPIO_NUM_2
#define PIN_RC522_CS      GPIO_NUM_10   
#define PIN_RC522_RST     GPIO_NUM_11

#define PIN_SERVO         GPIO_NUM_18
#define PIN_HCSR04_TRIG   GPIO_NUM_19
#define PIN_HCSR04_ECHO   GPIO_NUM_20  

#define PIN_LED_RED       GPIO_NUM_22
#define PIN_LED_GREEN     GPIO_NUM_21
#define PIN_LED_YELLOW    GPIO_NUM_3
#define PIN_BUZZER        GPIO_NUM_23   /* buzzer ACTIVO */

#define PIN_BTN_ENROLL    GPIO_NUM_0   
#define PIN_TILT          GPIO_NUM_1    
#define TILT_ACTIVE_LEVEL 0             

 *  PARAMETROS DE LA PLUMA

#define SERVO_MIN_US      500           
#define SERVO_MAX_US      2400          
#define SERVO_CLOSED_DEG  0.0f          
#define SERVO_OPEN_DEG    90.0f
#define SERVO_STEP_DEG    2.0f
#define SERVO_STEP_MS     15

#define PRESENCE_CM       30            
#define PRESENCE_DEBOUNCE 2             
#define SENSOR_PERIOD_MS  30

#define CLEAR_HOLD_MS         1500      
#define OPEN_NO_VEHICLE_MS    10000    
#define OPEN_MAX_MS           60000     

#define RFID_POLL_MS      120
#define TAG_DEBOUNCE_MS   2500          
#define ENROLL_WINDOW_MS  10000



#define CRED_SALT         "uni-pluma-v1"
#define READ_POINT        "urn:uni:loc:estac-norte:pluma-01"
