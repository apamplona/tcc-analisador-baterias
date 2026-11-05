#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <stdint.h>

#include "esp_err.h"


esp_err_t current_control_init(void);


void current_control_set_target(
    float target_a
);


void current_control_update(
    float measured_current_a
);


float current_control_get_target(void);


uint8_t current_control_get_dac(void);


float current_control_get_vdac_estimate(void);


float current_control_get_vref_estimate(void);


void current_control_stop(void);


#endif