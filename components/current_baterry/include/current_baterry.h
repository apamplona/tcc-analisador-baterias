#ifndef CURRENT_BATERRY_H
#define CURRENT_BATERRY_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"


esp_err_t current_baterry_init(
    adc_oneshot_unit_handle_t adc_handle
);


esp_err_t current_baterry_read(
    float *current_a
);


#endif