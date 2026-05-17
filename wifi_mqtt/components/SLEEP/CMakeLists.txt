idf_component_register(
    SRCS "sleep.c"
    INCLUDE_DIRS "."
    REQUIRES log driver esp_wifi esp_hw_support
)