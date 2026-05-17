#include "sleep.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * STM32 控制 ESP32 睡眠/工作
 * GPIO32 = 0：STM32 请求 ESP32 进入 Deep-sleep
 * GPIO32 = 1：STM32 请求 ESP32 工作 / 唤醒
 */
#define WAKE_CTRL_GPIO GPIO_NUM_32
static const char *TAG_SLEEP = "SLEEP_CTRL";


/*
 * 睡眠前处理。
 * 可以在这里暂停 MQTT、UART 解析任务、清空队列等。
 */
static void app_before_sleep(void)
{
    extern TaskHandle_t uart_task_handle;
    extern TaskHandle_t mqtt_send_handle;

    if (uart_task_handle != NULL) {
        vTaskSuspend(uart_task_handle);
    }
    // 看看队列是否有数据待上传，如果有最多等待1s
    extern QueueHandle_t HeartRateQueueHandle;
    int waitcount = 0;
    if (HeartRateQueueHandle != NULL) {
        while (uxQueueMessagesWaiting(HeartRateQueueHandle) > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waitcount++;
            ESP_LOGW(TAG_SLEEP, "Queue not empty, waitcount = %d, wait",waitcount);
            if (waitcount >= 100) {
                ESP_LOGW(TAG_SLEEP, "waitcount = %d, don't wait",waitcount);
                break;
            }
        }
    }

    if (mqtt_send_handle != NULL) {
        vTaskSuspend(mqtt_send_handle);
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
}



/*
 * GPIO32 = 0：睡眠
 * GPIO32 = 1：工作
 * 使用下拉
 */
static void sleep_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << WAKE_CTRL_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // 下拉
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}


/*
 * 根据 GPIO32 进入 Deep-sleep。
 */
static void enter_deep_sleep_by_gpio(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG_SLEEP, "进入深度睡眠模式");
    app_before_sleep();
    esp_sleep_enable_ext0_wakeup(WAKE_CTRL_GPIO, 1); // 通过外部引脚1唤醒
    esp_deep_sleep_start();
}

/*
 * 睡眠状态检测任务。
 * GPIO32 = 0：进入 Deep-sleep
 * GPIO32 = 1：正常工作
 */
void sleep_status_search(void *pvParameters)
{
    (void)pvParameters;
    sleep_gpio_init();
    while (1) {
        int level = gpio_get_level(WAKE_CTRL_GPIO);
        if (level == 0) {
            ESP_LOGI(TAG_SLEEP,"STM32 request sleep, GPIO%d=0",
                     WAKE_CTRL_GPIO);
            enter_deep_sleep_by_gpio();
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
