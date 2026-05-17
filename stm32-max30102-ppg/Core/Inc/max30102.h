#ifndef MAX30102WDD_MAX_H
#define MAX30102WDD_MAX_H

#include <stdint.h>
#include "main.h"

// 定义IIC和INT接口
#define MAX30102_IIC_PORT       GPIOB
#define MAX30102_INT_PIN        GPIO_PIN_12
#define MAX30102_IIC_SCL_PIN    GPIO_PIN_6
#define MAX30102_IIC_SDA_PIN    GPIO_PIN_7

// 定义MAX30102地址
#define MAX30102_ADDR_7BIT             0x57
#define MAX30102_ADDR_HAL              (MAX30102_ADDR_7BIT << 1)


// 定义芯片ID寄存器地址
#define MAX30102_REG_INTR_STATUS_1        0x00
#define MAX30102_REG_INTR_STATUS_2        0x01
#define MAX30102_REG_INTR_ENABLE_1        0x02
#define MAX30102_REG_INTR_ENABLE_2        0x03
#define MAX30102_REG_FIFO_WR_PTR          0x04
#define MAX30102_REG_OVF_COUNTER          0x05
#define MAX30102_REG_FIFO_RD_PTR          0x06
#define MAX30102_REG_FIFO_DATA            0x07
#define MAX30102_REG_FIFO_CONFIG          0x08
#define MAX30102_REG_MODE_CONFIG          0x09
#define MAX30102_REG_SPO2_CONFIG          0x0A
#define MAX30102_REG_LED1_PA              0x0C
#define MAX30102_REG_LED2_PA              0x0D
#define MAX30102_REG_MULTI_LED_CTRL1      0x11
#define MAX30102_REG_MULTI_LED_CTRL2      0x12
#define MAX30102_REG_DIE_TEMP_INT         0x1F
#define MAX30102_REG_TEMP_DIE_TEMP_FRAC   0x20
#define MAX30102_REG_DIE_TEMP_CONFIG      0x21
#define MAX30102_REG_REV_ID               0xFE
#define MAX30102_REG_PART_ID              0xFF

// 定义状态
#define MAX30102_MODE_RESET (1 << 6)

// 每读取中断寄存器或中断引发的寄存器，中断都会被清楚
// 中断1状态
#define FIFO_ALMOST_FULL_FLAG (1 << 7) // FIFO快满了再中断
#define FIFO_NEW_DATA_READY_FLAG (1 << 6) // FIFO有数据就中断
#define FAmbient_Light_Cancellation_Overflow_FLAG (1 << 5) // 环境光消除达到最大中断
// 中断2状态
#define Internal_Temperature_Ready_Flag (1 << 1) // 内部温度转换完成中断


// 模式配置
#define HEART_RATE_MODE (1<<1)
#define SPO2_MODE (0x11)

// 带通滤波器参数
typedef struct bandpass {
  float x1,x2,y1,y2;
  float b0,b1,b2,a1,a2;
} bandpass;

// 环形缓冲区
#define SAVE_HEART_DATA_SIZE 500
typedef struct heart_buff {
  float data[SAVE_HEART_DATA_SIZE];
  int head;
  int count;
} heart_buff_t;

HAL_StatusTypeDef MAX30102_ShutDown(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef MAX30102_Init(I2C_HandleTypeDef *hi2c,uint8_t mode);
HAL_StatusTypeDef  MAX30102_GetDataPointer(I2C_HandleTypeDef *hi2c,uint32_t *data_pointer);
HAL_StatusTypeDef MAX30102_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *data);
float  MAX30102_bandpass_filter(float current_value,bandpass *bandpass_Hz);
void MAX30102_BandPass_Init(bandpass *bandpass_Hz);
void Slide_Window_Init(heart_buff_t* hb);
void Slide_Window_AddData(heart_buff_t* hb,int newvalue);
int Slide_Window_GetData(heart_buff_t* hb,int index);
uint8_t Get_Fifo_left(I2C_HandleTypeDef *hi2c);

#endif //MAX30102WDD_MAX_H
