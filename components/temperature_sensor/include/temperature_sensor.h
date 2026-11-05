#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include "esp_err.h"


/*
 * Inicializa o DHT22.
 */
esp_err_t temperature_sensor_init(void);


/*
 * Realiza uma leitura do DHT22.
 *
 * temperature_c:
 * temperatura em graus Celsius.
 *
 * humidity_percent:
 * umidade relativa em %.
 */
esp_err_t temperature_sensor_read(
    float *temperature_c,
    float *humidity_percent
);


#endif