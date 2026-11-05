#include "voltage.h"

#include <stdbool.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_err.h"
#include "esp_log.h"


#define ADC_UNIT        ADC_UNIT_1

/*
 * GPIO34 = ADC1_CHANNEL_6
 */
#define ADC_CHANNEL     ADC_CHANNEL_6

#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_DEFAULT

#define NUM_SAMPLES     64


/*
 * Divisor resistivo:
 *
 * BAT+ --- 68k ---+--- 1k --- GPIO34
 *                 |
 *                12k
 *                 |
 *                GND
 */
#define R1              68000.0f
#define R2              12000.0f

#define DIVIDER_GAIN    ((R1 + R2) / R2)


static const char *TAG = "BATTERY_VOLTAGE";


static adc_oneshot_unit_handle_t adc_handle;

static adc_cali_handle_t cali_handle = NULL;

static bool calibration_enabled = false;


void voltage_init(void)
{
    /*
     * Cria ADC1.
     */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };


    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(
            &init_config,
            &adc_handle
        )
    );


    /*
     * Configura GPIO34.
     */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };


    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            ADC_CHANNEL,
            &channel_config
        )
    );


    /*
     * Calibracao ADC.
     */
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };


    esp_err_t ret =
        adc_cali_create_scheme_line_fitting(
            &cali_config,
            &cali_handle
        );


    if (ret == ESP_OK)
    {
        calibration_enabled = true;

        ESP_LOGI(
            TAG,
            "Calibracao ADC habilitada"
        );
    }
    else
    {
        calibration_enabled = false;

        ESP_LOGW(
            TAG,
            "Calibracao ADC nao disponivel"
        );
    }


    ESP_LOGI(
        TAG,
        "GPIO34 configurado para leitura da bateria"
    );
}


float voltage_read(void)
{
    int raw_sum = 0;


    /*
     * Media de 64 amostras.
     */
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        int raw = 0;


        ESP_ERROR_CHECK(
            adc_oneshot_read(
                adc_handle,
                ADC_CHANNEL,
                &raw
            )
        );


        raw_sum += raw;
    }


    int raw_average =
        raw_sum / NUM_SAMPLES;


    if (!calibration_enabled)
    {
        return -1.0f;
    }


    int voltage_mv = 0;


    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(
            cali_handle,
            raw_average,
            &voltage_mv
        )
    );


    /*
     * Tensao medida no GPIO34.
     */
    float vadc =
        voltage_mv / 1000.0f;


    /*
     * Reconstrucao da tensao da bateria.
     */
    float vbat =
        vadc * DIVIDER_GAIN;


    return vbat;
}


adc_oneshot_unit_handle_t voltage_get_adc_handle(void)
{
    return adc_handle;
}