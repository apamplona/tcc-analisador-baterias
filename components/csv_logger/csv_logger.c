#include "csv_logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"


#define CSV_BASE_PATH           "/spiffs"
#define CSV_PARTITION_LABEL     "storage"

#define CSV_MAX_FILES           999

/*
 * Faz flush a cada 10 registros.
 *
 * Se o main registrar a cada 500 ms,
 * teremos um flush aproximadamente
 * a cada 5 segundos.
 */
#define CSV_FLUSH_INTERVAL      10


static const char *TAG =
    "CSV_LOGGER";


static FILE *s_file =
    NULL;


static char s_file_path[64] =
    {0};


/*
 * Buffer para reduzir a quantidade
 * de escritas pequenas na flash.
 */
static char s_file_buffer[2048];


static int64_t s_start_time_us =
    0;


static int64_t s_last_time_us =
    0;


static float s_previous_current_a =
    0.0f;


static float s_previous_power_w =
    0.0f;


static bool s_has_previous_sample =
    false;


static double s_charge_ah =
    0.0;


static double s_energy_wh =
    0.0;


static unsigned int s_record_count =
    0;


/*
 * ============================================================
 * FORMATA FLOAT COM VIRGULA DECIMAL
 * ============================================================
 *
 * Isso deixa o arquivo mais amigavel
 * para Excel configurado em pt-BR.
 *
 * Exemplo:
 *
 * 1.234 -> 1,234
 */
static void format_float_ptbr(
    char *buffer,
    size_t buffer_size,
    double value,
    int decimals
)
{
    char format[16];


    snprintf(
        format,
        sizeof(format),
        "%%.%df",
        decimals
    );


    snprintf(
        buffer,
        buffer_size,
        format,
        value
    );


    for (
        char *p = buffer;
        *p != '\0';
        p++
    )
    {
        if (*p == '.')
        {
            *p = ',';
        }
    }
}


/*
 * ============================================================
 * CRIA NOME AUTOMATICO
 * ============================================================
 *
 * Procura:
 *
 * ensaio_001.csv
 * ensaio_002.csv
 * ...
 *
 * e utiliza o primeiro que ainda
 * nao existir.
 */
static esp_err_t create_new_filename(void)
{
    struct stat st;


    for (
        int i = 1;
        i <= CSV_MAX_FILES;
        i++
    )
    {
        snprintf(
            s_file_path,
            sizeof(s_file_path),
            CSV_BASE_PATH "/ensaio_%03d.csv",
            i
        );


        if (
            stat(
                s_file_path,
                &st
            ) != 0
        )
        {
            return ESP_OK;
        }
    }


    return ESP_ERR_NO_MEM;
}


/*
 * ============================================================
 * INICIALIZACAO
 * ============================================================
 */
esp_err_t csv_logger_init(void)
{
    /*
     * Configuracao SPIFFS.
     */
    esp_vfs_spiffs_conf_t conf = {
        .base_path =
            CSV_BASE_PATH,

        .partition_label =
            CSV_PARTITION_LABEL,

        .max_files =
            5,

        .format_if_mount_failed =
            true,
    };


    esp_err_t ret =
        esp_vfs_spiffs_register(
            &conf
        );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao montar SPIFFS: %s",
            esp_err_to_name(ret)
        );


        return ret;
    }


    /*
     * Mostra informacoes da memoria.
     */
    size_t total_bytes =
        0;


    size_t used_bytes =
        0;


    ret =
        esp_spiffs_info(
            CSV_PARTITION_LABEL,
            &total_bytes,
            &used_bytes
        );


    if (ret == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "SPIFFS: total = %u bytes | usado = %u bytes",
            (unsigned int)total_bytes,
            (unsigned int)used_bytes
        );
    }


    /*
     * Gera nome automatico.
     */
    ret =
        create_new_filename();


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar novo nome de arquivo"
        );


        return ret;
    }


    /*
     * Cria arquivo.
     */
    s_file =
        fopen(
            s_file_path,
            "w"
        );


    if (s_file == NULL)
    {
        ESP_LOGE(
            TAG,
            "Falha ao criar %s",
            s_file_path
        );


        return ESP_FAIL;
    }


    /*
     * Bufferiza a escrita.
     */
    setvbuf(
        s_file,
        s_file_buffer,
        _IOFBF,
        sizeof(s_file_buffer)
    );


    /*
     * Cabecalho.
     *
     * Separador = ;
     * Decimal = ,
     *
     * Isso funciona bem no Excel pt-BR.
     */
    fprintf(
        s_file,

        "tempo_s;"
        "tensao_bateria_V;"
        "corrente_A;"
        "corrente_ref_A;"
        "potencia_W;"
        "temperatura_C;"
        "umidade_pct;"
        "DAC;"
        "Vdac_V;"
        "Vref_V;"
        "carga_Ah;"
        "energia_Wh\n"
    );


    fflush(
        s_file
    );


    /*
     * Inicializa integradores.
     */
    s_start_time_us =
        esp_timer_get_time();


    s_last_time_us =
        s_start_time_us;


    s_previous_current_a =
        0.0f;


    s_previous_power_w =
        0.0f;


    s_has_previous_sample =
        false;


    s_charge_ah =
        0.0;


    s_energy_wh =
        0.0;


    s_record_count =
        0;


    ESP_LOGI(
        TAG,
        "Novo ensaio: %s",
        s_file_path
    );


    return ESP_OK;
}


/*
 * ============================================================
 * REGISTRO DE DADOS
 * ============================================================
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
)
{
    if (s_file == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }


    int64_t now_us =
        esp_timer_get_time();


    double elapsed_s =
        (
            now_us -
            s_start_time_us
        )
        / 1000000.0;


    /*
     * Potencia instantanea entregue
     * pela bateria.
     */
    float power_w =
        voltage_v *
        current_a;


    /*
     * Integracao temporal.
     *
     * Utilizamos regra trapezoidal
     * entre duas amostras consecutivas.
     */
    if (s_has_previous_sample)
    {
        double dt_h =
            (
                now_us -
                s_last_time_us
            )
            /
            3600000000.0;


        double average_current_a =
            (
                s_previous_current_a +
                current_a
            )
            / 2.0;


        double average_power_w =
            (
                s_previous_power_w +
                power_w
            )
            / 2.0;


        /*
         * Ah acumulados.
         */
        s_charge_ah +=
            average_current_a *
            dt_h;


        /*
         * Wh acumulados.
         */
        s_energy_wh +=
            average_power_w *
            dt_h;
    }


    /*
     * Atualiza estado para
     * proxima integracao.
     */
    s_previous_current_a =
        current_a;


    s_previous_power_w =
        power_w;


    s_last_time_us =
        now_us;


    s_has_previous_sample =
        true;


    /*
     * Strings formatadas
     * com virgula decimal.
     */
    char tempo[24];
    char tensao[24];
    char corrente[24];
    char corrente_ref[24];
    char potencia[24];

    char temperatura[24];
    char umidade[24];

    char vdac[24];
    char vref[24];

    char carga[24];
    char energia[24];


    format_float_ptbr(
        tempo,
        sizeof(tempo),
        elapsed_s,
        3
    );


    format_float_ptbr(
        tensao,
        sizeof(tensao),
        voltage_v,
        4
    );


    format_float_ptbr(
        corrente,
        sizeof(corrente),
        current_a,
        4
    );


    format_float_ptbr(
        corrente_ref,
        sizeof(corrente_ref),
        current_reference_a,
        4
    );


    format_float_ptbr(
        potencia,
        sizeof(potencia),
        power_w,
        4
    );


    if (temperature_valid)
    {
        format_float_ptbr(
            temperatura,
            sizeof(temperatura),
            temperature_c,
            2
        );


        format_float_ptbr(
            umidade,
            sizeof(umidade),
            humidity_percent,
            2
        );
    }
    else
    {
        temperatura[0] =
            '\0';


        umidade[0] =
            '\0';
    }


    format_float_ptbr(
        vdac,
        sizeof(vdac),
        vdac_v,
        4
    );


    format_float_ptbr(
        vref,
        sizeof(vref),
        vref_v,
        4
    );


    format_float_ptbr(
        carga,
        sizeof(carga),
        s_charge_ah,
        6
    );


    format_float_ptbr(
        energia,
        sizeof(energia),
        s_energy_wh,
        6
    );


    /*
     * Escreve uma linha.
     */
    int result =
        fprintf(
            s_file,

            "%s;"
            "%s;"
            "%s;"
            "%s;"
            "%s;"
            "%s;"
            "%s;"
            "%u;"
            "%s;"
            "%s;"
            "%s;"
            "%s\n",

            tempo,
            tensao,
            corrente,
            corrente_ref,
            potencia,
            temperatura,
            umidade,
            dac_value,
            vdac,
            vref,
            carga,
            energia
        );


    if (result < 0)
    {
        ESP_LOGE(
            TAG,
            "Erro ao gravar CSV"
        );


        return ESP_FAIL;
    }


    s_record_count++;


    /*
     * Flush periodico.
     *
     * Evita gravar a cada linha,
     * mas tambem reduz a quantidade
     * de dados perdidos caso a
     * alimentacao seja interrompida.
     */
    if (
        s_record_count %
        CSV_FLUSH_INTERVAL == 0
    )
    {
        fflush(
            s_file
        );
    }


    return ESP_OK;
}


/*
 * ============================================================
 * FLUSH
 * ============================================================
 */
esp_err_t csv_logger_flush(void)
{
    if (s_file == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }


    if (
        fflush(
            s_file
        ) != 0
    )
    {
        return ESP_FAIL;
    }


    return ESP_OK;
}


/*
 * ============================================================
 * RETORNA CAMINHO
 * ============================================================
 */
const char *csv_logger_get_path(void)
{
    return s_file_path;
}


/*
 * ============================================================
 * FECHA ARQUIVO
 * ============================================================
 */
esp_err_t csv_logger_close(void)
{
    if (s_file == NULL)
    {
        return ESP_OK;
    }


    fflush(
        s_file
    );


    fclose(
        s_file
    );


    s_file =
        NULL;


    ESP_LOGI(
        TAG,
        "Arquivo salvo: %s",
        s_file_path
    );


    return ESP_OK;
}


/*
 * ============================================================
 * EXPORTA PARA TERMINAL
 * ============================================================
 */
esp_err_t csv_logger_dump_to_console(void)
{
    if (s_file != NULL)
    {
        fflush(
            s_file
        );
    }


    FILE *file =
        fopen(
            s_file_path,
            "r"
        );


    if (file == NULL)
    {
        return ESP_FAIL;
    }


    printf(
        "\n===== CSV_BEGIN =====\n"
    );


    char line[256];


    while (
        fgets(
            line,
            sizeof(line),
            file
        ) != NULL
    )
    {
        printf(
            "%s",
            line
        );
    }


    printf(
        "===== CSV_END =====\n"
    );


    fclose(
        file
    );


    return ESP_OK;
}