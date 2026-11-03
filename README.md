 Cableado

| Componente | Pin del módulo | GPIO ESP32-C6 | Notas |
|---|---|---|---|
| PN532 | 5V | 5V | Regulador a bordo |
| PN532 | GND | GND | |
| PN532 | SCK | 6 | |
| PN532 | MI (MISO) | 2 | Mide que no pase de 3.3 V antes de conectar |
| PN532 | MO/SDA/TX (MOSI) | 7 | |
| PN532 | NSS/SCL/RX (CS) | 10 | |
| PN532 | RST | 11 | |
| PN532 | IRQ | sin conectar | |
| Servo SG90 | Señal (naranja) | 18 | |
| Servo SG90 | Rojo / Café | 5V / GND | GND común |
| HC-SR04 | VCC / GND | 5V / GND | |
| HC-SR04 | TRIG | 19 | |
| HC-SR04 | ECHO | 20 | **Con divisor de voltaje** |
| LED rojo | ánodo vía 220 Ω | 22 | cátodo a GND |
| LED verde | ánodo vía 220 Ω | 21 | cátodo a GND |
| LED amarillo | ánodo vía 220 Ω | 3 | cátodo a GND |
| Buzzer activo | + | 23 | − a GND |
| Botón registro | un lado | 0 | otro lado a GND |
| Tilt switch | un lado | 1 | otro lado a GND |
