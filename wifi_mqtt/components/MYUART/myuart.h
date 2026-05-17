#ifndef WIFI_MQTT_MYUART_H
#define WIFI_MQTT_MYUART_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
  int32_t ppg[100];       // 400 bytes
  uint16_t heartrate;     // 2 bytes
  uint16_t valley_count; // 峰谷的数量
  uint16_t valley_index[20]; // 心率最高240
} Trans_Data_t;

void uart_init();
void uart_task(void *pvParameters);

#endif //WIFI_MQTT_MYUART_H
