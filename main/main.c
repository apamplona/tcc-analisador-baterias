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
 * Tensao minima permitida para a bateria.
 *
 * Exemplo para bateria Li-ion 3S:
 *
 * 3.2 V por celula:
 *
 * 3 x 3.2 V = 9.6 V
 *
 * Ajustar posteriormente de acordo
 * com a bateria utilizada no ensaio.
 */
#define BATTERY_CUTOFF_V             9.60f


/*
 * Numero de leituras consecutivas
 * abaixo da tensao minima necessarias
 * para encerrar o ensaio.
 *
 * Com periodo de 100 ms:
 *
 * 5 leituras = aproximadamente 500 ms.
 */
#define LOW_VOLTAGE_CONFIRM_COUNT    5


/*
 * Perfil de corrente utilizado para
 * simular uma carga variavel.
 *
 * Sequencia:
 *
 * 1 A -> 2 A -> 3 A -> 3 A -> 2 A -> 1 A
 *
 * Ao chegar ao final, o perfil e
 * reiniciado enquanto a bateria nao
 * atingir a tensao de corte.
 */
static const float CURRENT_PROFILE_A[] =
{
    1.0f,
    2.0f,
    3.0f,
    3.0f,
    2.0f,
    1.0f
};


#define CURRENT_PROFILE_LENGTH       \
    (sizeof(CURRENT_PROFILE_A) /     \
     sizeof(CURRENT_PROFILE_A[0]))


/*
 * Tempo de permanencia em cada
 * patamar do perfil de corrente.
 *
 * 30 segundos por patamar.
 *
 * O ciclo completo dura:
 *
 * 6 x 30 = 180 segundos.
 */
#define CURRENT_PROFILE_STEP_SECONDS 30.0


/*
 * Periodo da malha de controle.
 *
 * 100 ms = 10 Hz.
 */
#define CONTROL_PERIOD_MS            100


/*
 * Registro no terminal e CSV.
 *
 * 5 x 100 ms = aproximadamente 500 ms.
 *
 * 2 registros por segundo.
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


    /*
     * Inicia o ensaio no primeiro
     * patamar do perfil: 1 A.
     */
    current_control_set_target(
        CURRENT_PROFILE_A[0]
    );


    /*
     * Instante real de inicio.
     */
    int64_t test_start_time_us =
        esp_timer_get_time();



    printf(
        "\n========================================\n"
        "ENSAIO INICIADO\n"
        "Modo: perfil de corrente variavel\n"
        "Perfil: 1 -> 2 -> 3 -> 3 -> 2 -> 1 A\n"
        "Tempo por patamar: %.0f s\n"
        "Tensao de corte: %.2f V\n"
        "========================================\n\n",

        CURRENT_PROFILE_STEP_SECONDS,
        BATTERY_CUTOFF_V
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


    int low_voltage_counter =
        0;


    int last_profile_index =
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
         * TENSAO DA BATERIA
         * ----------------------------------------------------
         *
         * A tensao agora e lida em todos
         * os ciclos da malha, pois ela
         * define o criterio de parada.
         */

        float vbat =
            voltage_read();



        /*
         * ----------------------------------------------------
         * CRITERIO DE PARADA POR TENSAO
         * ----------------------------------------------------
         *
         * Sao exigidas varias leituras
         * consecutivas abaixo do limite
         * para evitar parada causada por
         * uma amostra isolada.
         */

        if (
            vbat > 0.0f &&
            vbat <= BATTERY_CUTOFF_V
        )
        {
            low_voltage_counter++;
        }
        else
        {
            low_voltage_counter =
                0;
        }



        if (
            low_voltage_counter >=
            LOW_VOLTAGE_CONFIRM_COUNT
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
                "Tensao minima atingida.\n"
                "Vbat: %.3f V\n"
                "Limite: %.3f V\n"
                "Tempo total: %.2f s\n"
                "Descarga interrompida.\n"
                "========================================\n\n",

                vbat,
                BATTERY_CUTOFF_V,
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
         * PERFIL DE CORRENTE
         * ----------------------------------------------------
         *
         * Calcula qual patamar deve estar
         * ativo de acordo com o tempo.
         */

        int profile_index =
            ((int)(
                elapsed_seconds /
                CURRENT_PROFILE_STEP_SECONDS
            ))
            %
            (int)CURRENT_PROFILE_LENGTH;



        /*
         * So altera a referencia quando
         * ocorre uma mudanca de patamar.
         *
         * Isso e importante porque
         * current_control_set_target()
         * reaplica o feedforward.
         */

        if (
            profile_index !=
            last_profile_index
        )
        {
            last_profile_index =
                profile_index;


            current_control_set_target(
                CURRENT_PROFILE_A[
                    profile_index
                ]
            );


            printf(
                "\n"
                "Mudanca de carga: "
                "Iref = %.1f A | "
                "t = %.1f s\n\n",

                CURRENT_PROFILE_A[
                    profile_index
                ],

                elapsed_seconds
            );
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
         * CONTROLE DE CORRENTE
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
             * Dados atuais do controle.
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