idf_component_register(SRCS "main.c" "control.c" "serial_input.c" "serial_transport.c" INCLUDE_DIRS "." REQUIRES esp_driver_uart esp_driver_gpio esp_timer nvs_flash)
