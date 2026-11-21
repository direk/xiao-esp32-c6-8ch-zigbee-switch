idf_component_register(SRCS "main.c" "control.c" INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_timer nvs_flash)
