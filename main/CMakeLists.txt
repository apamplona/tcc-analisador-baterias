idf_component_register(
    SRCS "main.c"

    INCLUDE_DIRS "."

    REQUIRES
        battery_voltage
        current_baterry
        current_control
        temperature_sensor
        csv_logger
)