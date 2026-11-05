#ifndef SERIAL_EXPORT_H
#define SERIAL_EXPORT_H

#include "esp_err.h"


esp_err_t serial_export_init(void);


/*
 * Deve ser chamada periodicamente
 * no loop principal.
 *
 * Verifica se chegou algum comando
 * pela USB/serial.
 */
void serial_export_poll(void);


#endif