idf_component_register(
        SRCS "mqtt.c"
        INCLUDE_DIRS "."
        REQUIRES MYUART
        PRIV_REQUIRES mqtt esp_event esp_netif log
)