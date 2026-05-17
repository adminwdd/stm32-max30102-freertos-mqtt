#ifndef __MQTT_H
#define __MQTT_H

#define MQTT_ADDRESS CONFIG_MQTT_BROKER_URI
#define MQTT_CLIENTID CONFIG_MQTT_CLIENT_ID
#define MQTT_USERNAME CONFIG_MQTT_USERNAME
#define MQTT_PASSWORD CONFIG_MQTT_PASSWORD
#define TAG "mqtt"
#define MQTT_TOPIC CONFIG_MQTT_COMMAND_TOPIC
#define MQTT_PUBLISH_TOPIC CONFIG_MQTT_PPG_TOPIC
#define MQTT_STATUS_TOPIC CONFIG_MQTT_STATUS_TOPIC
#define MQTT_ONLINE_MSG "{\"client_id\":\"" CONFIG_MQTT_CLIENT_ID "\",\"status\":\"online\"}"
#define MQTT_OFFLINE_MSG "{\"client_id\":\"" CONFIG_MQTT_CLIENT_ID "\",\"status\":\"offline\"}"

void mqtt_init(void);
void mqtt_send(void *pvParameters);

#endif

