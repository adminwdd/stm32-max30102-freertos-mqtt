#include <stdio.h>
#include "wifista.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt.h"
#include "esp_log.h"
#include "mqtt.h"
#include "myuart.h"
#include "sleep.h"

SemaphoreHandle_t wifi_connected_handel = NULL;
BaseType_t wifi_connected_status = pdFALSE;
TaskHandle_t uart_task_handle = NULL;
TaskHandle_t mqtt_send_handle = NULL;


void app_main(void)
{
  uart_init();
  // 创建二值信号量，默认为0
  wifi_connected_handel = xSemaphoreCreateBinary();
  // 初始化wifi
  wifista_init();
  xSemaphoreTake(wifi_connected_handel, portMAX_DELAY);
  mqtt_init();
  // 创建发送主题任务
  xTaskCreatePinnedToCore(uart_task, "uart_task", 6144, NULL, 5, &uart_task_handle,1);
  xTaskCreatePinnedToCore(mqtt_send, "mqtt_send", 6144, NULL, 5, &mqtt_send_handle,1);
  xTaskCreatePinnedToCore(sleep_status_search, "sleep_status_search", 3072, NULL, 6, NULL,1);
}
