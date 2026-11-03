
## Cableado

| Componente | Pin del módulo | GPIO ESP32-C6 | Notas |
|---|---|---|---|
| RC522 | 3.3V / GND | 3V3 / GND | **Solo 3.3 V** |
| RC522 | SCK | 6 | SPI |
| RC522 | MOSI | 7 | SPI |
| RC522 | MISO | 2 | SPI |
| RC522 | SDA (CS) | 10 | |
| RC522 | RST | 11 | |
| RC522 | IRQ | sin conectar | |
| Servo SG90 | Señal (naranja) | 18 | |
| Servo SG90 | Rojo / Café | 5V / GND | GND común con el ESP32 |
| HC-SR04 | VCC / GND | 5V / GND | |
| HC-SR04 | TRIG | 19 | |
| HC-SR04 | ECHO | 20 | **Con divisor de voltaje** |
| LED rojo | ánodo vía 220 Ω | 22 | cátodo a GND |
| LED verde | ánodo vía 220 Ω | 21 | cátodo a GND |
| LED amarillo | ánodo vía 220 Ω | 3 | cátodo a GND |
| Buzzer activo | + | 23 | − a GND |
| Botón registro | un lado | 0 | otro lado a GND (pull-up interno) |
| Tilt switch | un lado | 1 | otro lado a GND (pull-up interno) |
