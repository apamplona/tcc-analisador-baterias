#include "temperature_sensor.h"

#include <stdint.h>

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"


/*
 * GPIO utilizado pelo DHT22.
 */
#define DHT_GPIO                GPIO_NUM_33


/*
 * O DHT22 nao deve ser lido
 * continuamente.
 *
 * Intervalo minimo utilizado:
 * 2 segundos.
 */
#define DHT_MIN_INTERVAL_US     2000000LL


/*
 * Limiar utilizado para distinguir
 * bit 0 de bit 1.
 *
 * DHT22:
 *
 * bit 0 -> pulso alto ~26 us
 * bit 1 -> pulso alto ~70 us
 */
#define DHT_BIT_THRESHOLD_US    50


static const char *TAG =
    "TEMPERATURE_SENSOR";


static int64_t last_read_time_us =
    -DHT_MIN_INTERVAL_US;


/*
 * Aguarda o pino atingir determinado nivel.
 *
 * Retorna ESP_ERR_TIMEOUT se o sensor
 * nao responder dentro do tempo esperado.
 */
static esp_err_t wait_for_level(
    int level,
    uint32_t timeout_us
)
{
    int64_t start =
        esp_timer_get_time();


    while (gpio_get_level(DHT_GPIO) != level)
    {
        if (
            (esp_timer_get_time() - start)
            > timeout_us
        )
        {
            return ESP_ERR_TIMEOUT;
        }
    }


    return ESP_OK;
}


esp_err_t temperature_sensor_init(void)
{
    /*
     * Open-drain:
     *
     * nivel 0 -> ESP32 puxa a linha para GND
     * nivel 1 -> ESP32 libera a linha
     *
     * O resistor de pull-up leva a linha
     * para 3.3 V.
     */
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << DHT_GPIO),

        .mode =
            GPIO_MODE_INPUT_OUTPUT_OD,

        .pull_up_en =
            GPIO_PULLUP_ENABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE,
    };


    esp_err_t ret =
        gpio_config(
            &config
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * Deixa a linha inicialmente
     * liberada.
     */
    gpio_set_level(
        DHT_GPIO,
        1
    );


    ESP_LOGI(
        TAG,
        "DHT22 inicializado na GPIO33"
    );


    return ESP_OK;
}


esp_err_t temperature_sensor_read(
    float *temperature_c,
    float *humidity_percent
)
{
    if (
        temperature_c == NULL ||
        humidity_percent == NULL
    )
    {
        return ESP_ERR_INVALID_ARG;
    }


    /*
     * O DHT22 necessita de intervalo
     * entre leituras.
     */
    int64_t now =
        esp_timer_get_time();


    if (
        (now - last_read_time_us)
        < DHT_MIN_INTERVAL_US
    )
    {
        return ESP_ERR_INVALID_STATE;
    }


    last_read_time_us =
        now;


    /*
     * ====================================
     * SINAL DE START
     * ====================================
     *
     * Host puxa DATA para zero por
     * aproximadamente 1.2 ms.
     */
    gpio_set_level(
        DHT_GPIO,
        0
    );


    esp_rom_delay_us(
        1200
    );


    /*
     * Libera a linha.
     */
    gpio_set_level(
        DHT_GPIO,
        1
    );


    /*
     * Aguarda o DHT detectar
     * o fim do start.
     */
    esp_rom_delay_us(
        30
    );


    /*
     * ====================================
     * RESPOSTA DO DHT22
     * ====================================
     */


    /*
     * Sensor puxa DATA para LOW.
     */
    esp_err_t ret =
        wait_for_level(
            0,
            100
        );


    if (ret != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "DHT22 nao respondeu"
        );

        return ret;
    }


    /*
     * Depois sobe para HIGH.
     */
    ret =
        wait_for_level(
            1,
            100
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * E volta para LOW antes
     * do primeiro bit.
     */
    ret =
        wait_for_level(
            0,
            100
        );


    if (ret != ESP_OK)
    {
        return ret;
    }


    /*
     * ====================================
     * RECEPCAO DOS 40 BITS
     * ====================================
     */

    uint8_t data[5] = {
        0,
        0,
        0,
        0,
        0
    };


    for (int bit = 0; bit < 40; bit++)
    {
        /*
         * Cada bit comeca com aproximadamente
         * 50 us em LOW.
         *
         * Aguarda a subida.
         */
        ret =
            wait_for_level(
                1,
                100
            );


        if (ret != ESP_OK)
        {
            return ret;
        }


        /*
         * Marca o inicio do pulso HIGH.
         */
        int64_t high_start =
            esp_timer_get_time();


        /*
         * Aguarda o fim do pulso HIGH.
         */
        ret =
            wait_for_level(
                0,
                120
            );


        if (ret != ESP_OK)
        {
            return ret;
        }


        int64_t high_duration =
            esp_timer_get_time()
            - high_start;


        /*
         * Desloca byte atual.
         */
        int byte_index =
            bit / 8;


        data[byte_index] <<= 1;


        /*
         * Pulso maior que aproximadamente
         * 50 us -> bit 1.
         *
         * Menor -> bit 0.
         */
        if (
            high_duration >
            DHT_BIT_THRESHOLD_US
        )
        {
            data[byte_index] |= 1;
        }
    }


    /*
     * ====================================
     * CHECKSUM
     * ====================================
     */

    uint8_t checksum =
        (uint8_t)(
            data[0]
            + data[1]
            + data[2]
            + data[3]
        );


    if (checksum != data[4])
    {
        ESP_LOGW(
            TAG,
            "Erro de checksum do DHT22"
        );


        return ESP_ERR_INVALID_CRC;
    }


    /*
     * ====================================
     * UMIDADE
     * ====================================
     */

    uint16_t humidity_raw =
        ((uint16_t)data[0] << 8)
        | data[1];


    *humidity_percent =
        humidity_raw / 10.0f;


    /*
     * ====================================
     * TEMPERATURA
     * ====================================
     */

    uint16_t temperature_raw =
        ((uint16_t)(data[2] & 0x7F) << 8)
        | data[3];


    float temperature =
        temperature_raw / 10.0f;


    /*
     * Bit mais significativo indica
     * temperatura negativa.
     */
    if (data[2] & 0x80)
    {
        temperature =
            -temperature;
    }


    *temperature_c =
        temperature;


    return ESP_OK;
}