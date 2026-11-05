#ifndef CSV_LOGGER_H
#define CSV_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"


/*
 * Inicializa o sistema de arquivos
 * e cria um novo arquivo:
 *
 * ensaio_001.csv
 * ensaio_002.csv
 * ensaio_003.csv
 * ...
 */
esp_err_t csv_logger_init(void);


/*
 * Registra uma amostra.
 *
 * O proprio modulo calcula:
 *
 * - tempo decorrido
 * - potencia instantanea
 * - Ah acumulados
 * - Wh acumulados
 */
esp_err_t csv_logger_log(
    float voltage_v,
    float current_a,
    float current_reference_a,
    float temperature_c,
    float humidity_percent,
    bool temperature_valid,
    uint8_t dac_value,
    float vdac_v,
    float vref_v
);


/*
 * Forca os dados pendentes
 * a serem gravados na flash.
 */
esp_err_t csv_logger_flush(void);


/*
 * Fecha o arquivo.
 */
esp_err_t csv_logger_close(void);


/*
 * Retorna o caminho do arquivo atual.
 */
const char *csv_logger_get_path(void);


/*
 * Imprime todo o arquivo CSV
 * no terminal.
 *
 * Pode ser usado posteriormente
 * para exportar o ensaio.
 */
esp_err_t csv_logger_dump_to_console(void);


#endif