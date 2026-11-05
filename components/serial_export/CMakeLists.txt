idf_component_register(
    SRCS "serial_export.c"

    INCLUDE_DIRS "include"

    REQUIRES
        csv_logger
        current_control
)