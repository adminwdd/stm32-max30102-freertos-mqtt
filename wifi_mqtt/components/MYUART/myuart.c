#include "myuart.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>

#define TAG "UART_RX"
#define UART_PORT               UART_NUM_1

#define STARTBYTE1              0xAA
#define STARTBYTE2              0x55

#define ENDBYTE1                0x0D
#define ENDBYTE2                0x0D

#define UART_PAYLOAD_LEN        ((uint16_t)sizeof(Trans_Data_t))   // 444
#define UART_FRAME_HEADER_LEN   6                                  // AA 55 + seq*2 + len*2
#define UART_CRC_LEN            2
#define UART_END_LEN            2
#define UART_FRAME_LEN          (UART_FRAME_HEADER_LEN + UART_PAYLOAD_LEN + UART_CRC_LEN + UART_END_LEN) // 454

// 强制对齐
typedef struct __attribute__((packed)) {
    uint8_t  head1;                     // 帧头 0xAA
    uint8_t  head2;                     // 帧头 0x55
    uint16_t seq;                       // 帧序号
    uint16_t len;                       // 载荷长度
    uint8_t  payload[UART_PAYLOAD_LEN]; // 数据体
    uint16_t crc;                       // CRC校验
    uint8_t  tail1;                     // 帧尾 0x0D
    uint8_t  tail2;                     // 帧尾 0x0D
} uart_frame_t;

// 接收状态机枚举
typedef enum {
    RX_WAIT_HEAD1 = 0,     // 等待第一个帧头
    RX_WAIT_HEAD2 = 1,     // 等待第二个帧头
    RX_RECEIVE_FRAME = 2   // 接收后续的完整帧数据
} uart_rx_state_t;

uint16_t g_seq_failed = 0;

// 队列相关变量
QueueHandle_t HeartRateQueueHandle = NULL;
StaticQueue_t xQueueTransferBuffer;
uint8_t QueueTransferBuffer[10 * sizeof(Trans_Data_t)];

static uint8_t rx_buf[128];

// CRC计算
static uint16_t calc_crc16_uint16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// 把数据放到队列中
static void queue_send_frame(const Trans_Data_t *data)
{
    if (HeartRateQueueHandle == NULL) {
        return;
    }
    BaseType_t ret = xQueueSendToBack(HeartRateQueueHandle, data, 0);
    if (ret != pdPASS) {
        Trans_Data_t dump;
        // 队列满时丢掉最旧的一帧，保留最新数据
        xQueueReceive(HeartRateQueueHandle, &dump, 0);
        xQueueSendToBack(HeartRateQueueHandle, data, 0);

        ESP_LOGW(TAG, "Queue full, dropped oldest frame");
    }
}

// 串口接收与解析任务
void uart_task(void *pvParameters)
{
    (void)pvParameters;

    // --- 队列初始化 ---
    if (HeartRateQueueHandle == NULL) {
        HeartRateQueueHandle = xQueueCreateStatic(
            10,
            sizeof(Trans_Data_t),
            QueueTransferBuffer,
            &xQueueTransferBuffer
        );
    }
    if (HeartRateQueueHandle == NULL) {
        ESP_LOGE(TAG, "Failed to create UART transfer queue");
        vTaskDelete(NULL);
        return;
    }

    // --- 状态机相关变量 ---
    uart_rx_state_t rx_state = RX_WAIT_HEAD1;
    static uint8_t frame_buf[UART_FRAME_LEN]; // 刚好装下一帧的缓存
    uint16_t rx_index = 0;                    // 当前写到了第几个字节

    static bool has_last_seq = false;
    static uint16_t last_seq = 0;

    while (1) {
        // 读取串口数据
        int len = uart_read_bytes(UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        // 3. 将收到的数据逐个字节喂给状态机
        for (int i = 0; i < len; i++) {
            uint8_t byte = rx_buf[i];
            switch (rx_state) {
                // 状态一：寻找 AA
                case RX_WAIT_HEAD1:
                    if (byte == STARTBYTE1) {
                        frame_buf[0] = byte;
                        rx_index = 1;
                        rx_state = RX_WAIT_HEAD2; // 找到 AA，进入下一状态找 55
                    }
                    break;

                // 状态二：寻找 55
                case RX_WAIT_HEAD2:
                    if (byte == STARTBYTE2) {
                        frame_buf[1] = byte;
                        rx_index = 2;
                        rx_state = RX_RECEIVE_FRAME; // 头找齐了，开始接收数据体
                    } else if (byte == STARTBYTE1) {
                        // 容错：如果连续收到 AA AA 55
                        frame_buf[0] = byte;
                        rx_index = 1;
                    } else {
                        // 找错了，重新回去找 AA
                        rx_state = RX_WAIT_HEAD1;
                    }
                    break;

                // 状态三：接收剩余的一整帧
                case RX_RECEIVE_FRAME:
                    frame_buf[rx_index++] = byte;

                    // 如果接收满了 454 个字节，就进行一次完整校验
                    if (rx_index >= UART_FRAME_LEN) {
                        uart_frame_t *frame = (uart_frame_t *)frame_buf;
                        bool is_valid = true;

                        // 校验长度和帧尾
                        if (frame->len != UART_PAYLOAD_LEN || frame->tail1 != ENDBYTE1 || frame->tail2 != ENDBYTE2) {
                            is_valid = false;
                        }
                        // 校验 CRC (跳过2个字节帧头，计算后面的 frame->len + 4 个字节：seq + len + payload)
                        else {
                            uint16_t calc_crc = calc_crc16_uint16((uint8_t *)&frame->seq, frame->len + 4);
                            if (frame->crc != calc_crc) {
                                ESP_LOGW(TAG, "CRC error: rx=0x%04X, calc=0x%04X", frame->crc, calc_crc);
                                is_valid = false;
                            }
                        }

                        // 如果校验全部通过，处理数据
                        if (is_valid) {
                            // 检查序号丢包
                            if (has_last_seq && frame->seq != (uint16_t)(last_seq + 1)) {
                                g_seq_failed++;
                                ESP_LOGW(TAG, "Frame lost/disorder: last=%u, current=%u", last_seq, frame->seq);
                            }
                            last_seq = frame->seq;
                            has_last_seq = true;

                            // 提取 payload 发送到队列
                            Trans_Data_t data;
                            memcpy(&data, frame->payload, sizeof(Trans_Data_t));
                            queue_send_frame(&data);

                            // --- 还原数据最值判断与打印逻辑 ---
                            int32_t min_v = data.ppg[0];
                            int32_t max_v = data.ppg[0];
                            int max_i = 0;
                            int min_i = 0;

                            for (int idx = 1; idx < 100; idx++) {
                                if (data.ppg[idx] > max_v) {
                                    max_v = data.ppg[idx];
                                    max_i = idx;
                                }
                                if (data.ppg[idx] < min_v) {
                                    min_v = data.ppg[idx];
                                    min_i = idx;
                                }
                            }

                            ESP_LOGI(TAG,
                                     "seq=%u hr=%u valley=%u ppg[0]=%" PRId32 " ppg[1]=%" PRId32
                                     " ppg[2]=%" PRId32 " ppg[50]=%" PRId32 " ppg[99]=%" PRId32
                                     " min=%" PRId32 "@%d max=%" PRId32 "@%d",
                                     frame->seq,
                                     data.heartrate,
                                     data.valley_count,
                                     data.ppg[0],
                                     data.ppg[1],
                                     data.ppg[2],
                                     data.ppg[50],
                                     data.ppg[99],
                                     min_v,
                                     min_i,
                                     max_v,
                                     max_i);
                            // ESP_LOGW(TAG, "Frame has lost %d", g_seq_failed);
                            ESP_LOGI(TAG, "Frame OK: seq=%u, hr=%u, ppg0=%ld, lost frame = %d",
                                     frame->seq,
                                     data.heartrate,
                                     (long)data.ppg[0],
                                     g_seq_failed);
                        }
                        // 无论这帧是对是错，处理完后一律重置状态，去寻找下一个 AA
                        rx_state = RX_WAIT_HEAD1;
                    }
                    break;
            }
        }
    }
}

// 串口初始化
void uart_init(void)
{
    uart_config_t uart_structure = {
        .baud_rate = 230400,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 100,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_structure));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, -1, -1));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_flush_input(UART_NUM_1));
}
