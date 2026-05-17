#include "wifista.h"
#include "esp_wifi.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"

void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if(event_base == WIFI_EVENT){
        if(event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI("TAG_WIFI","wifi connected");
            esp_wifi_connect();
        }
        else if(event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI("TAG_WIFI","wifi disconnected");
            esp_wifi_connect();
        }
        else if(event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI("TAG_WIFI","wifi connected successfully");
        }
    }
    else if(event_base == IP_EVENT){
        if(event_id == IP_EVENT_STA_GOT_IP) {
            esp_netif_ip_info_t *event = (esp_netif_ip_info_t *)event_data;
            uint8_t ip1 = esp_ip4_addr1_16(&event->ip);
            uint8_t ip2 = esp_ip4_addr2_16(&event->ip);
            uint8_t ip3 = esp_ip4_addr3_16(&event->ip);
            uint8_t ip4 = esp_ip4_addr4_16(&event->ip);
            ESP_LOGI("TAG_WIFI","wifi got ip: %d.%d.%d.%d", ip1, ip2, ip3, ip4);

            extern SemaphoreHandle_t wifi_connected_handel;
            extern BaseType_t wifi_connected_status ;
            wifi_connected_status = xSemaphoreGive(wifi_connected_handel);
        }
    }
}

void wifista_init(void) {
    // wifi的ssid和pwd需要nvs flash保存
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi in station mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PASSWORD,
        },
    };
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}

