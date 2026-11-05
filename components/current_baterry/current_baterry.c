#include "current_baterry.h"

#include <stdbool.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"


/*
 * GPIO35 = ADC1_CHANNEL_7
 */
#define CURRENT_ADC_CHANNEL          ADC_CHANNEL_7
#define CURRENT_ADC_ATTEN            ADC_ATTEN_DB_12
#define CURRENT_ADC_BITWIDTH         ADC_BITWIDTH_DEFAULT


/*
 * Numero de amostras utilizadas na media.
 */
#define NUM_SAMPLES                  64


/*
 * Shunt utilizado no circuito:
 *
 * Rshunt = 200 milliohm = 0.2 ohm
 */
#define SHUNT_RESISTANCE_OHM         1.0f


/*
 * LM358 de medicao de corrente:
 *
 * R5 = 10k
 * R6 = 1k
 *
 * Configuracao nao inversora:
 *
 * G = 1 + R6/R5
 * G = 1 + 1k/10k
 * G = 1.1
 */
#define CURRENT_AMPLIFIER_GAIN       1.1f


static const char *TAG =
    "CURRENT_BATERRY";


static adc_oneshot_unit_handle_t
    s_adc_handle;


static adc_cali_handle_t
    s_cali_handle = NULL;


static bool
    s_calibration_enabled = false;


esp_err_t current_baterry_init(
    adc_oneshot_unit_handle_t adc_handle
)
{
    if (adc_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    /*
     * Reutiliza o ADC1 criado
     * pelo modulo de tensao.
     */
    s_adc_handle =
        adc_handle;


    /*
     * Configura GPIO35.
     */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = CURRENT_ADC_ATTEN,
        .bitwidth = CURRENT_ADC_BITWIDTH,
    };


    esp_err_t ret =
        adc_oneshot_config_channel(
            s_adc_handle,
            CURRENT_ADC_CHANNEL,
            &channel_config
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * Calibracao do ADC.
     */
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = CURRENT_ADC_ATTEN,
        .bitwidth = CURRENT_ADC_BITWIDTH,
    };


    ret =
        adc_cali_create_scheme_line_fitting(
            &cali_config,
            &s_cali_handle
        );


    if (ret == ESP_OK)
    {
        s_calibration_enabled =
            true;


        ESP_LOGI(
            TAG,
            "Calibracao ADC da corrente habilitada"
        );
    }
    else
    {
        s_calibration_enabled =
            false;


        ESP_LOGE(
            TAG,
            "Falha na calibracao ADC da corrente"
        );


        return ret;
    }


    ESP_LOGI(
        TAG,
        "GPIO35 configurado para leitura da corrente"
    );


    ESP_LOGI(
        TAG,
        "Shunt = %.3f ohm | Ganho = %.2f",
        SHUNT_RESISTANCE_OHM,
        CURRENT_AMPLIFIER_GAIN
    );


    return ESP_OK;
}


esp_err_t current_baterry_read(
    float *current_a
)
{
    if (current_a == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (!s_calibration_enabled)
    {
        return ESP_ERR_INVALID_STATE;
    }


    int raw_sum = 0;


    /*
     * Media de varias amostras ADC.
     */
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        int raw = 0;


        esp_err_t ret =
            adc_oneshot_read(
                s_adc_handle,
                CURRENT_ADC_CHANNEL,
                &raw
            );


        if (ret != ESP_OK)
        {
            return ret;
        }


        raw_sum += raw;
    }


    int raw_average =
        raw_sum / NUM_SAMPLES;


    /*
     * Converte ADC para tensao.
     */
    int voltage_mv = 0;


    esp_err_t ret =
        adc_cali_raw_to_voltage(
            s_cali_handle,
            raw_average,
            &voltage_mv
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * Tensao na GPIO35, em volts.
     */
    float amplifier_voltage =
        voltage_mv / 1000.0f;


    /*
     * Remove o ganho do LM358:
     *
     * Vshunt = Vout / 1.1
     */
    float shunt_voltage =
        amplifier_voltage /
        CURRENT_AMPLIFIER_GAIN;


    /*
     * Calcula a corrente:
     *
     * I = Vshunt / Rshunt
     *
     * Com Rshunt = 0.2 ohm e G = 1.1:
     *
     * I = Vgpio35 / (1.1 * 0.2)
     * I = Vgpio35 / 0.22
     */
    *current_a =
        shunt_voltage /
        SHUNT_RESISTANCE_OHM;


    return ESP_OK;
}