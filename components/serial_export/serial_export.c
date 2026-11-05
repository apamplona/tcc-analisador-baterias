#include "serial_export.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "csv_logger.h"
#include "current_control.h"


#define COMMAND_BUFFER_SIZE 64


static const char *TAG =
    "SERIAL_EXPORT";


static char command_buffer[
    COMMAND_BUFFER_SIZE
];


static size_t command_length =
    0;



/*
 * ============================================================
 * PROCESSAMENTO DOS COMANDOS
 * ============================================================
 */
static void process_command(
    const char *command
)
{
    /*
     * --------------------------------------------------------
     * EXPORTAR
     * --------------------------------------------------------
     *
     * Encerra a descarga e envia
     * o arquivo CSV atual pela serial.
     */
    if (
        strcmp(
            command,
            "EXPORTAR"
        ) == 0
    )
    {
        ESP_LOGI(
            TAG,
            "Comando EXPORTAR recebido"
        );


        /*
         * Segurança:
         *
         * desliga a descarga antes
         * de transmitir o arquivo.
         */
        current_control_stop();


        /*
         * Garante que todos os dados
         * pendentes foram gravados.
         */
        esp_err_t ret =
            csv_logger_flush();


        if (ret != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Falha ao executar flush do CSV"
            );

            return;
        }


        ESP_LOGI(
            TAG,
            "Descarga encerrada"
        );


        ESP_LOGI(
            TAG,
            "Exportando arquivo..."
        );


        /*
         * Envia o CSV completo
         * pela serial.
         */
        ret =
            csv_logger_dump_to_console();


        if (ret != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Falha ao exportar CSV"
            );

            return;
        }


        ESP_LOGI(
            TAG,
            "Exportacao concluida"
        );


        return;
    }


    /*
     * --------------------------------------------------------
     * ARQUIVO
     * --------------------------------------------------------
     *
     * Mostra qual arquivo esta
     * sendo gravado atualmente.
     */
    if (
        strcmp(
            command,
            "ARQUIVO"
        ) == 0
    )
    {
        printf(
            "\nArquivo atual: %s\n\n",
            csv_logger_get_path()
        );


        return;
    }


    /*
     * --------------------------------------------------------
     * FLUSH
     * --------------------------------------------------------
     */
    if (
        strcmp(
            command,
            "FLUSH"
        ) == 0
    )
    {
        esp_err_t ret =
            csv_logger_flush();


        if (ret == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "CSV sincronizado com a flash"
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Erro no flush do CSV"
            );
        }


        return;
    }


    ESP_LOGW(
        TAG,
        "Comando desconhecido: %s",
        command
    );
}



/*
 * ============================================================
 * INICIALIZACAO
 * ============================================================
 */
esp_err_t serial_export_init(void)
{
    command_length =
        0;


    memset(
        command_buffer,
        0,
        sizeof(command_buffer)
    );


    ESP_LOGI(
        TAG,
        "Controle serial inicializado"
    );


    ESP_LOGI(
        TAG,
        "Comandos: EXPORTAR | ARQUIVO | FLUSH"
    );


    return ESP_OK;
}



/*
 * ============================================================
 * LEITURA DA SERIAL
 * ============================================================
 *
 * O stdin do ESP-IDF usa a UART
 * configurada como console.
 *
 * A leitura padrao e nao bloqueante.
 */
void serial_export_poll(void)
{
    int character;


    /*
     * Le todos os caracteres
     * atualmente disponiveis.
     */
    while (
        (
            character =
                getchar()
        )
        != EOF
    )
    {
        /*
         * ENTER recebido.
         */
        if (
            character == '\n' ||
            character == '\r'
        )
        {
            /*
             * Ignora ENTER vazio.
             */
            if (
                command_length > 0
            )
            {
                command_buffer[
                    command_length
                ] = '\0';


                process_command(
                    command_buffer
                );


                command_length =
                    0;


                memset(
                    command_buffer,
                    0,
                    sizeof(command_buffer)
                );
            }


            continue;
        }


        /*
         * Adiciona caractere
         * ao buffer.
         */
        if (
            command_length <
            (
                COMMAND_BUFFER_SIZE -
                1
            )
        )
        {
            command_buffer[
                command_length
            ] =
                (char)character;


            command_length++;
        }
        else
        {
            /*
             * Buffer cheio:
             * descarta comando.
             */
            command_length =
                0;


            memset(
                command_buffer,
                0,
                sizeof(command_buffer)
            );


            ESP_LOGW(
                TAG,
                "Comando serial muito longo"
            );
        }
    }


    /*
     * Como stdin e nao bloqueante,
     * getchar() pode marcar EOF
     * quando nao existe caractere.
     *
     * Limpamos a flag para permitir
     * novas leituras posteriormente.
     */
    clearerr(
        stdin
    );
}