idf_component_register(SRCS "wifi_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES WIFISTA APP_MQTT mqtt esp_event MYUART SLEEP
)
