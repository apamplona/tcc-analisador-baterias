idf_component_register(
    SRCS "csv_logger.c"

    INCLUDE_DIRS "include"

    REQUIRES
        spiffs
        esp_timer
)