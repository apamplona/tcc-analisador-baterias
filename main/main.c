#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"

#include "voltage.h"
#include "current_baterry.h"
#include "current_control.h"
#include "temperature_sensor.h"
#include "csv_logger.h"


/*
 * ============================================================
 * CONFIGURACOES DO ENSAIO
 * ============================================================
 */


/*
 * Corrente de descarga.
 */
#define CURRENT_TARGET_A             1.500f


/*
 * Duracao total do ensaio.
 *
 * 10 minutos:
 *
 * 10 x 60 = 600 segundos
 */
#define TEST_DURATION_SECONDS        600.0


/*
 * Periodo da malha de controle.
 *
 * 100 ms = 10 Hz
 */
#define CONTROL_PERIOD_MS            100


/*
 * Registro no terminal e CSV.
 *
 * 5 x 100 ms = aproximadamente 500 ms
 *
 * 2 amostras por segundo.
 */
#define DATA_LOG_DIVIDER             5


/*
 * DHT22:
 *
 * leitura aproximadamente
 * a cada 2.5 segundos.
 */
#define TEMPERATURE_READ_DIVIDER     25



void app_main(void)
{
    /*
     * ========================================================
     * TENSAO DA BATERIA
     * ========================================================
     */

    voltage_init();


    adc_oneshot_unit_handle_t adc_handle =
        voltage_get_adc_handle();



    /*
     * ========================================================
     * CORRENTE
     * ========================================================
     */

    ESP_ERROR_CHECK(
        current_baterry_init(
            adc_handle
        )
    );



    /*
     * ========================================================
     * TEMPERATURA
     * ========================================================
     */

    ESP_ERROR_CHECK(
        temperature_sensor_init()
    );



    /*
     * ========================================================
     * CONTROLE DE CORRENTE
     * ========================================================
     */

    ESP_ERROR_CHECK(
        current_control_init()
    );



    /*
     * ========================================================
     * LOGGER CSV
     * ========================================================
     */

    ESP_ERROR_CHECK(
        csv_logger_init()
    );


    printf(
        "\nArquivo CSV: %s\n",
        csv_logger_get_path()
    );



    /*
     * ========================================================
     * INICIO DO ENSAIO
     * ========================================================
     */

    current_control_set_target(
        CURRENT_TARGET_A
    );


    /*
     * Marca o instante real de inicio.
     *
     * esp_timer_get_time() retorna
     * microssegundos.
     */
    int64_t test_start_time_us =
        esp_timer_get_time();


    printf(
        "\n========================================\n"
        "ENSAIO INICIADO\n"
        "Corrente alvo: %.3f A\n"
        "Duracao: %.0f segundos\n"
        "========================================\n\n",

        CURRENT_TARGET_A,
        TEST_DURATION_SECONDS
    );



    /*
     * ========================================================
     * VARIAVEIS
     * ========================================================
     */

    float temperature_c =
        0.0f;


    float humidity_percent =
        0.0f;


    bool temperature_valid =
        false;



    int data_log_counter =
        0;


    int temperature_counter =
        0;



    /*
     * ========================================================
     * LOOP DO ENSAIO
     * ========================================================
     */

    while (1)
    {
        /*
         * ----------------------------------------------------
         * TEMPO DECORRIDO
         * ----------------------------------------------------
         */

        int64_t now_us =
            esp_timer_get_time();


        double elapsed_seconds =
            (now_us - test_start_time_us)
            / 1000000.0;



        /*
         * ----------------------------------------------------
         * VERIFICA FIM DO ENSAIO
         * ----------------------------------------------------
         */

        if (
            elapsed_seconds >=
            TEST_DURATION_SECONDS
        )
        {
            /*
             * ================================================
             * ENCERRA A DESCARGA
             * ================================================
             */

            current_control_stop();


            printf(
                "\n\n========================================\n"
                "FIM DO ENSAIO\n"
                "Tempo decorrido: %.2f s\n"
                "Descarga interrompida.\n"
                "========================================\n\n",

                elapsed_seconds
            );


            /*
             * ================================================
             * GARANTE GRAVACAO DO CSV
             * ================================================
             */

            ESP_ERROR_CHECK(
                csv_logger_flush()
            );


            /*
             * Fecha o arquivo antes
             * de fazer a leitura completa.
             */
            ESP_ERROR_CHECK(
                csv_logger_close()
            );


            /*
             * ================================================
             * IMPRIME CSV NO SERIAL MONITOR
             * ================================================
             */

            printf(
                "\n"
                "Copie o conteudo entre "
                "CSV_BEGIN e CSV_END.\n\n"
            );


            ESP_ERROR_CHECK(
                csv_logger_dump_to_console()
            );


            /*
             * ================================================
             * PARA O PROGRAMA
             * ================================================
             *
             * A descarga permanece desligada.
             *
             * O ESP32 fica aqui ate ser
             * reiniciado.
             */

            while (1)
            {
                vTaskDelay(
                    pdMS_TO_TICKS(
                        1000
                    )
                );
            }
        }



        /*
         * ----------------------------------------------------
         * LEITURA DA CORRENTE
         * ----------------------------------------------------
         */

        float ibat =
            0.0f;


        ESP_ERROR_CHECK(
            current_baterry_read(
                &ibat
            )
        );



        /*
         * ----------------------------------------------------
         * CONTROLE
         * ----------------------------------------------------
         */

        current_control_update(
            ibat
        );



        /*
         * ----------------------------------------------------
         * TEMPERATURA
         * ----------------------------------------------------
         */

        temperature_counter++;


        if (
            temperature_counter >=
            TEMPERATURE_READ_DIVIDER
        )
        {
            temperature_counter =
                0;


            esp_err_t temp_ret =
                temperature_sensor_read(
                    &temperature_c,
                    &humidity_percent
                );


            if (
                temp_ret ==
                ESP_OK
            )
            {
                temperature_valid =
                    true;
            }
        }



        /*
         * ----------------------------------------------------
         * REGISTRO
         * ----------------------------------------------------
         */

        data_log_counter++;


        if (
            data_log_counter >=
            DATA_LOG_DIVIDER
        )
        {
            data_log_counter =
                0;



            /*
             * Tensao da bateria.
             */
            float vbat =
                voltage_read();



            /*
             * Dados do controle.
             */
            float iref =
                current_control_get_target();


            uint8_t dac_value =
                current_control_get_dac();


            float vdac_est =
                current_control_get_vdac_estimate();


            float vref_est =
                current_control_get_vref_estimate();



            /*
             * -----------------------------------------------
             * CSV
             * -----------------------------------------------
             */

            ESP_ERROR_CHECK(
                csv_logger_log(
                    vbat,
                    ibat,
                    iref,
                    temperature_c,
                    humidity_percent,
                    temperature_valid,
                    dac_value,
                    vdac_est,
                    vref_est
                )
            );



            /*
             * -----------------------------------------------
             * SERIAL MONITOR
             * -----------------------------------------------
             */

            if (
                temperature_valid
            )
            {
                printf(
                    "t = %.1f s | "
                    "Vbat = %.3f V | "
                    "Ibat = %.3f A | "
                    "Iref = %.3f A | "
                    "Temp = %.1f C | "
                    "UR = %.1f %% | "
                    "DAC = %u | "
                    "Vdac ~= %.3f V | "
                    "Vref ~= %.3f V\n",

                    elapsed_seconds,
                    vbat,
                    ibat,
                    iref,
                    temperature_c,
                    humidity_percent,
                    dac_value,
                    vdac_est,
                    vref_est
                );
            }
            else
            {
                printf(
                    "t = %.1f s | "
                    "Vbat = %.3f V | "
                    "Ibat = %.3f A | "
                    "Iref = %.3f A | "
                    "Temp = --- | "
                    "UR = --- | "
                    "DAC = %u | "
                    "Vdac ~= %.3f V | "
                    "Vref ~= %.3f V\n",

                    elapsed_seconds,
                    vbat,
                    ibat,
                    iref,
                    dac_value,
                    vdac_est,
                    vref_est
                );
            }
        }



        /*
         * ----------------------------------------------------
         * PERIODO DA MALHA
         * ----------------------------------------------------
         */

        vTaskDelay(
            pdMS_TO_TICKS(
                CONTROL_PERIOD_MS
            )
        );
    }
}