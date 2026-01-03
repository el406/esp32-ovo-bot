idf_component_register(SRCS "hallowcam.c" "esp_websocket_wifi.c" "servo.c" 
    INCLUDE_DIRS "." PRIV_REQUIRES esp_wifi json esp_driver_gpio nvs_flash esp_driver_ledc)


