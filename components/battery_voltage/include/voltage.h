#ifndef VOLTAGE_H
#define VOLTAGE_H

#include "esp_adc/adc_oneshot.h"

void voltage_init(void);

float voltage_read(void);

adc_oneshot_unit_handle_t voltage_get_adc_handle(void);

#endif