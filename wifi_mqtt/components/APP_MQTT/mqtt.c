#include "mqtt.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "myuart.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdbool.h>

// mqtt 句柄
static esp_mqtt_client_handle_t mqtt_handle = NULL;
static volatile bool mqtt_connected = false;

static void mqtt_publish_online_status(void)
{
  int msg_id = esp_mqtt_client_publish(mqtt_handle,
                                       MQTT_STATUS_TOPIC,
                                       MQTT_ONLINE_MSG,
                                       strlen(MQTT_ONLINE_MSG),
                                       1,
                                       1); // 设置保留消息
  if (msg_id >= 0)
  {
    ESP_LOGI(TAG, "MQTT status published online, msg_id=%d", msg_id);
  } else
  {
    ESP_LOGE(TAG, "MQTT status publish failed");
  }
}

void mqtt_send(void *pvParameters)
{
  (void)pvParameters;
  extern QueueHandle_t HeartRateQueueHandle;
  BaseType_t QueueReceiveStatus;
  Trans_Data_t hr_ppg_data;
  char mqtt_pub_buff[1536];

  while (HeartRateQueueHandle == NULL) {
    ESP_LOGW(TAG, "Waiting for UART queue");
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  while(1) {
    QueueReceiveStatus = xQueueReceive(HeartRateQueueHandle, &hr_ppg_data, portMAX_DELAY);
    if (QueueReceiveStatus == pdPASS) {
      if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, drop hr=%u", hr_ppg_data.heartrate);
        continue;
      }

      // 将数据格式化为JSON字符串
      int json_len = snprintf(mqtt_pub_buff, sizeof(mqtt_pub_buff),
                              "{\"heartrate\":%u,\"ppg\":[",
                              hr_ppg_data.heartrate);

      // 添加PPG数组数据
      for (int i = 0; i < 100 && json_len < sizeof(mqtt_pub_buff) - 200; i++) {
        json_len += snprintf(mqtt_pub_buff + json_len, sizeof(mqtt_pub_buff) - json_len,
                              "%s",  (i == 0) ? "" : ",");

        json_len += snprintf(mqtt_pub_buff + json_len,
                             sizeof(mqtt_pub_buff) - json_len,
                             "%ld", hr_ppg_data.ppg[i]);
      }
      // 添加峰谷及其索引下标
      json_len += snprintf(mqtt_pub_buff + json_len,
                           sizeof(mqtt_pub_buff) - json_len,
                           "],\"valley_count\":%u,\"valley_index\":[",
                           hr_ppg_data.valley_count);

      uint16_t count = hr_ppg_data.valley_count;
      if (count > 20) {
        count = 20;
      }

      for (int i = 0; i < count; i++)
      {
        json_len += snprintf(mqtt_pub_buff + json_len,
                             sizeof(mqtt_pub_buff) - json_len,
                             "%s%u",
                             (i == 0) ? "" : ",",
                             hr_ppg_data.valley_index[i]);
      }
      json_len += snprintf(mqtt_pub_buff + json_len,
                           sizeof(mqtt_pub_buff) - json_len,
                           "]}");
      if (json_len <= 0 || json_len >= sizeof(mqtt_pub_buff))
      {
        ESP_LOGE(TAG, "MQTT JSON buffer overflow, json_len=%d", json_len);
        continue;
      }

      // 发布到MQTT
      int msg_id = esp_mqtt_client_publish(mqtt_handle,
                                           MQTT_PUBLISH_TOPIC,
                                           mqtt_pub_buff,
                                           strlen(mqtt_pub_buff),
                                           1,   // QoS=1
                                           1);  // 保留消息
      if (msg_id >= 0) {
        ESP_LOGI(TAG, "MQTT published, msg_id=%d, hr=%u",
                 msg_id, hr_ppg_data.heartrate);
        ESP_LOGI(TAG, "indexlenth = %d", hr_ppg_data.valley_count);
      } else {
        ESP_LOGE(TAG, "MQTT publish failed");
      }
    }
  }
}

// 回调函数
void mqtt_event_callback(void* event_handler_arg, esp_event_base_t event_base,int32_t event_id,void* event_data)
{
  esp_mqtt_event_handle_t data = (esp_mqtt_event_handle_t)event_data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      mqtt_connected = true;
      ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
      esp_mqtt_client_subscribe(mqtt_handle,MQTT_TOPIC,1);
      mqtt_publish_online_status();
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqtt_connected = false;
      ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", ((esp_mqtt_event_handle_t)event_data)->msg_id);
      break;
    case MQTT_EVENT_PUBLISHED:
      ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED ACK ");
      break;
    case MQTT_EVENT_ERROR:
      mqtt_connected = false;
      if (data->error_handle != NULL) {
        ESP_LOGE(TAG,
                 "MQTT_EVENT_ERROR type=%d esp_tls=0x%x tls_stack=0x%x sock_errno=%d conn_ret=%d",
                 data->error_handle->error_type,
                 (unsigned int)data->error_handle->esp_tls_last_esp_err,
                 (unsigned int)data->error_handle->esp_tls_stack_err,
                 data->error_handle->esp_transport_sock_errno,
                 data->error_handle->connect_return_code);
      } else {
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
      }
      break;
    default:break;
  }
}

void mqtt_init(void)
{
  // 初始化mqtt
  esp_mqtt_client_config_t mqtt_cfg = {0};
  mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
  mqtt_cfg.broker.address.port = 1883;
  mqtt_cfg.credentials.client_id = MQTT_CLIENTID;
  mqtt_cfg.credentials.username = MQTT_USERNAME;
  mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
  // 遗嘱消息
  mqtt_cfg.session.last_will.topic = MQTT_STATUS_TOPIC;
  mqtt_cfg.session.last_will.msg = MQTT_OFFLINE_MSG;
  mqtt_cfg.session.last_will.msg_len = strlen(MQTT_OFFLINE_MSG);
  mqtt_cfg.session.last_will.qos = 2;
  mqtt_cfg.session.last_will.retain = 1;
  // 初始化
  mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
  // 注册回调函数
  esp_mqtt_client_register_event(mqtt_handle,ESP_EVENT_ANY_ID,mqtt_event_callback,NULL);
  // 启动mqtt
  esp_mqtt_client_start(mqtt_handle);
}

