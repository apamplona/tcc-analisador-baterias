#include "current_control.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/dac_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"


/*
 * ESP32:
 *
 * DAC_CHAN_0 = GPIO25
 */
#define DAC_CHANNEL                 DAC_CHAN_0


/*
 * DAC de 8 bits.
 */
#define DAC_MAX_VALUE               255


/*
 * Tensao maxima medida
 * experimentalmente no GPIO25.
 */
#define DAC_FULL_SCALE_V            3.28f


/*
 * Shunt utilizado no circuito:
 *
 * Rshunt = 200 milliohm = 0.2 ohm
 */
#define SHUNT_RESISTANCE_OHM        1.0f


/*
 * Corrente maxima permitida
 * pelo software.
 *
 * O alvo inicial continua sendo 1 A,
 * definido no main.c.
 */
#define MAX_CURRENT_A               10.0f


/*
 * Com Rshunt = 0.2 ohm:
 *
 * 1 LSB do DAC corresponde aproximadamente a:
 *
 * (3.28 / 255) / 0.2 = 0.064 A
 *
 * Por isso a banda morta foi definida
 * um pouco acima de um LSB.
 */
#define CURRENT_DEADBAND_A       0.015f //1R
//#define CURRENT_DEADBAND_A       0.075f // 0.2R



/*
 * Faixas de erro utilizadas
 * para determinar o passo de correcao.
 */
#define CURRENT_STEP_SMALL_A     0.025f //1R
#define CURRENT_STEP_MEDIUM_A    0.100f

//#define CURRENT_STEP_SMALL_A     0.200f //0.2R
//#define CURRENT_STEP_MEDIUM_A    1.000f

/*
 * Passos incrementais do DAC.
 */
#define DAC_STEP_SMALL              1
#define DAC_STEP_MEDIUM             2
#define DAC_STEP_LARGE              4


static const char *TAG =
    "CURRENT_CONTROL";


static dac_oneshot_handle_t
    dac_handle = NULL;


static float
    target_current_a = 0.0f;


static uint8_t
    dac_value = 0;


static bool
    control_initialized = false;


/*
 * Limita o DAC entre 0 e 255.
 */
static uint8_t clamp_dac(
    int value
)
{
    if (value < 0)
    {
        return 0;
    }


    if (value > DAC_MAX_VALUE)
    {
        return DAC_MAX_VALUE;
    }


    return (uint8_t)value;
}


/*
 * Atualiza fisicamente a GPIO25.
 */
static esp_err_t update_dac_output(void)
{
    return dac_oneshot_output_voltage(
        dac_handle,
        dac_value
    );
}


/*
 * Calcula a tensao aproximada
 * presente fisicamente na GPIO25.
 */
float current_control_get_vdac_estimate(void)
{
    return
        ((float)dac_value /
        (float)DAC_MAX_VALUE)
        * DAC_FULL_SCALE_V;
}


/*
 * Nao existe mais o divisor 100k/1k.
 *
 * O DAC chega ao Vref apenas pelo filtro RC,
 * portanto, em regime permanente:
 *
 * Vref ~= Vdac
 */
float current_control_get_vref_estimate(void)
{
    return current_control_get_vdac_estimate();
}


/*
 * Calcula o valor inicial do DAC
 * a partir da corrente desejada.
 */
static uint8_t current_to_dac(
    float current_a
)
{
    /*
     * A malha analogica do LM358 procura:
     *
     * Vshunt ~= Vref
     *
     * Como:
     *
     * Vshunt = I * Rshunt
     *
     * entao:
     *
     * Vref = Iref * Rshunt
     */
    float required_vref =
        current_a *
        SHUNT_RESISTANCE_OHM;


    /*
     * Sem divisor resistivo:
     *
     * Vdac ~= Vref
     *
     * Converte a tensao necessaria
     * para o codigo de 8 bits do DAC.
     */
    float dac_float =
        (required_vref /
        DAC_FULL_SCALE_V)
        * DAC_MAX_VALUE;


    int dac_int =
        (int)(dac_float + 0.5f);


    return clamp_dac(
        dac_int
    );
}


esp_err_t current_control_init(void)
{
    dac_oneshot_config_t config = {
        .chan_id = DAC_CHANNEL,
    };


    esp_err_t ret =
        dac_oneshot_new_channel(
            &config,
            &dac_handle
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * Comeca obrigatoriamente
     * com corrente zero.
     */
    dac_value =
        0;


    target_current_a =
        0.0f;


    ret =
        update_dac_output();


    if (ret != ESP_OK)
    {
        return ret;
    }


    control_initialized =
        true;


    ESP_LOGI(
        TAG,
        "Controle de corrente inicializado"
    );


    ESP_LOGI(
        TAG,
        "GPIO25 configurado como DAC"
    );


    ESP_LOGI(
        TAG,
        "Shunt = %.3f ohm",
        SHUNT_RESISTANCE_OHM
    );


    return ESP_OK;
}


void current_control_set_target(
    float target_a
)
{
    if (!control_initialized)
    {
        return;
    }


    /*
     * Impede corrente negativa.
     */
    if (target_a < 0.0f)
    {
        target_a =
            0.0f;
    }


    /*
     * Limite maximo.
     */
    if (target_a > MAX_CURRENT_A)
    {
        target_a =
            MAX_CURRENT_A;


        ESP_LOGW(
            TAG,
            "Corrente limitada para %.2f A",
            MAX_CURRENT_A
        );
    }


    target_current_a =
        target_a;


    /*
     * Corrente zero:
     * desliga o DAC.
     */
    if (target_current_a <= 0.0f)
    {
        dac_value =
            0;


        ESP_ERROR_CHECK(
            update_dac_output()
        );


        return;
    }


    /*
     * ==================================
     * FEEDFORWARD
     * ==================================
     *
     * Calcula diretamente o ponto
     * aproximado do DAC para a corrente alvo.
     */
    dac_value =
        current_to_dac(
            target_current_a
        );


    ESP_ERROR_CHECK(
        update_dac_output()
    );


    ESP_LOGI(
        TAG,
        "Nova corrente alvo: %.3f A",
        target_current_a
    );


    ESP_LOGI(
        TAG,
        "Feedforward: DAC = %u | "
        "Vdac ~= %.3f V | "
        "Vref ~= %.3f V",

        dac_value,
        current_control_get_vdac_estimate(),
        current_control_get_vref_estimate()
    );
}


void current_control_update(
    float measured_current_a
)
{
    if (!control_initialized)
    {
        return;
    }


    /*
     * Corrente alvo zero:
     * mantem DAC desligado.
     */
    if (target_current_a <= 0.0f)
    {
        if (dac_value != 0)
        {
            dac_value =
                0;


            ESP_ERROR_CHECK(
                update_dac_output()
            );
        }


        return;
    }


    /*
     * Erro:
     *
     * positivo  -> corrente abaixo do alvo
     * negativo  -> corrente acima do alvo
     */
    float error =
        target_current_a -
        measured_current_a;


    float abs_error =
        (error >= 0.0f)
        ? error
        : -error;


    /*
     * Dentro da banda morta:
     * nao altera o DAC.
     */
    if (
        abs_error <=
        CURRENT_DEADBAND_A
    )
    {
        return;
    }


    int step;


    /*
     * Define o tamanho da correcao.
     */
    if (
        abs_error >
        CURRENT_STEP_MEDIUM_A
    )
    {
        step =
            DAC_STEP_LARGE;
    }
    else if (
        abs_error >
        CURRENT_STEP_SMALL_A
    )
    {
        step =
            DAC_STEP_MEDIUM;
    }
    else
    {
        step =
            DAC_STEP_SMALL;
    }


    /*
     * Corrente abaixo do alvo:
     * aumenta Vref.
     */
    if (error > 0.0f)
    {
        dac_value =
            clamp_dac(
                (int)dac_value + step
            );
    }

    /*
     * Corrente acima do alvo:
     * diminui Vref.
     */
    else
    {
        dac_value =
            clamp_dac(
                (int)dac_value - step
            );
    }


    ESP_ERROR_CHECK(
        update_dac_output()
    );
}


float current_control_get_target(void)
{
    return target_current_a;
}


uint8_t current_control_get_dac(void)
{
    return dac_value;
}


void current_control_stop(void)
{
    target_current_a =
        0.0f;


    dac_value =
        0;


    if (control_initialized)
    {
        ESP_ERROR_CHECK(
            update_dac_output()
        );
    }


    ESP_LOGI(
        TAG,
        "Controle de corrente desligado"
    );
}