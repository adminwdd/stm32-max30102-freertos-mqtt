#include "max30102.h"
#include "OLED.h"
#include "freertos.h"
#include "task.h"

// 判断Interrupt Status
// MAX30102 INT falling edge
volatile uint32_t g_max30102IntCount  = 0;
extern TaskHandle_t xLowPowerTaskHandle;
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == MAX30102_INT_PIN) {
    g_max30102IntCount ++;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    extern TaskHandle_t xMax30102TaskHandle;
    // 如果任务已经被创建了且调度器running
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && xMax30102TaskHandle != NULL) {
      vTaskNotifyGiveFromISR(xMax30102TaskHandle,&xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
  else if (GPIO_Pin == GPIO_PIN_3 ) {
    BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
    extern volatile uint8_t g_stop_mode;
    if (g_stop_mode == 0)
    {
      vTaskNotifyGiveFromISR(xLowPowerTaskHandle,&pxHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
    }
  }
}

// 写MAX30102指定寄存器
HAL_StatusTypeDef MAX30102_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t data)
{
  return HAL_I2C_Mem_Write(hi2c,
                           MAX30102_ADDR_HAL,
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &data,
                           1,
                           100);
}

// 读MAX30102指定寄存器
HAL_StatusTypeDef MAX30102_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *data)
{
  return HAL_I2C_Mem_Read(hi2c,
                          MAX30102_ADDR_HAL,
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          data,
                          1,
                          100);
}

// 复位
HAL_StatusTypeDef MAX30102_Reset(I2C_HandleTypeDef *hi2c)
{
  HAL_StatusTypeDef ret;
  ret = MAX30102_WriteReg(hi2c, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_RESET);

  if (ret != HAL_OK) return ret;
  HAL_Delay(10);
  return HAL_OK;
}

// 清除4个FIFO寄存器
HAL_StatusTypeDef MAX30102_ClearFIFO(I2C_HandleTypeDef *hi2c)
{
  HAL_StatusTypeDef ret;

  ret = MAX30102_WriteReg(hi2c, MAX30102_REG_FIFO_WR_PTR, 0x00);
  if (ret != HAL_OK) return ret;

  ret = MAX30102_WriteReg(hi2c, MAX30102_REG_OVF_COUNTER, 0x00);
  if (ret != HAL_OK) return ret;

  ret = MAX30102_WriteReg(hi2c, MAX30102_REG_FIFO_RD_PTR, 0x00);
  if (ret != HAL_OK) return ret;


  return HAL_OK;
}

// 初始化函数
HAL_StatusTypeDef MAX30102_Init(I2C_HandleTypeDef *hi2c,uint8_t mode)
{
  HAL_StatusTypeDef ret;
  uint8_t device_id = 0;
  // 检查设备是否存在，最多重复三次
  ret = HAL_I2C_IsDeviceReady(hi2c, MAX30102_ADDR_HAL, 3, 100);
  if (ret != HAL_OK) return ret;

  // 检查设备id是否为0x15
  ret = MAX30102_ReadReg(hi2c, MAX30102_REG_PART_ID, &device_id);
  if (ret != HAL_OK) return ret;
  if (device_id != 0x15) return HAL_ERROR;

  // 软件复位
  ret = MAX30102_Reset(hi2c);
  if (ret != HAL_OK) return ret;

  // 清除中断寄存器的状态
  uint8_t dummy;
  ret = MAX30102_ReadReg(hi2c, MAX30102_REG_INTR_STATUS_1, &dummy);
  if (ret != HAL_OK) return ret;
  ret = MAX30102_ReadReg(hi2c, MAX30102_REG_INTR_STATUS_2, &dummy);
  if (ret != HAL_OK) return ret;
  ret = MAX30102_ClearFIFO(hi2c);
  if (ret != HAL_OK) return ret;

  // 有新数据就触发一次中断
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_INTR_ENABLE_1,FIFO_NEW_DATA_READY_FLAG);
  if (ret != HAL_OK) return ret;

  //FIFO配置 B7 B6 B5 配置样本平均字000表示1,最大到32,1<<6 表示4
  // 满了不覆盖
  // 当剩余0个数据空闲空间的时候，，即有32个FIFO都没读设置为0x0
  // 当剩余15个数据空闲空间的时候，即有17个FIFO没读，设置为0xF
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_FIFO_CONFIG,(0x01 << 5) | (0x00 << 4) | 0x0F); // 2次平均
  if (ret != HAL_OK) return ret;

  // 模式设置
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_MODE_CONFIG,HEART_RATE_MODE);
  if (ret != HAL_OK) return ret;

  // 设置LED电流
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_LED1_PA,0x24);
  if (ret != HAL_OK) return ret;

  // 设置ADC Range 15pA - 4096nA
  // 每秒采样率200Hz,2次平均，真实采样率为100Hz
  // 脉宽411us 分辨率18bit
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_SPO2_CONFIG,(0x01 << 5) | (0x01 << 3) | 0x03); // 采样率未平均前200Hz
  if (ret != HAL_OK) return ret;

  ret = MAX30102_WriteReg(hi2c, MAX30102_REG_LED2_PA, 0x00);
  if (ret != HAL_OK) return ret;

  ret = MAX30102_ClearFIFO(hi2c);
  if (ret != HAL_OK) return ret;

  return  HAL_OK;
}


HAL_StatusTypeDef MAX30102_ShutDown(I2C_HandleTypeDef *hi2c)
{
  HAL_StatusTypeDef ret;
  ret = MAX30102_WriteReg(hi2c,MAX30102_REG_MODE_CONFIG,(1<<7));
  return ret;
}


// 读取1个3字节sample
HAL_StatusTypeDef MAX30102_GetDataPointer(I2C_HandleTypeDef *hi2c, uint32_t *data_pointer)
{
  HAL_StatusTypeDef ret;
  uint8_t data[3];

  ret = HAL_I2C_Mem_Read(hi2c,
                           MAX30102_ADDR_HAL,
                           MAX30102_REG_FIFO_DATA,
                           I2C_MEMADD_SIZE_8BIT,
                           data,
                           3,
                           100);
    if (ret != HAL_OK) return ret;
    *data_pointer = (((uint32_t)data[0] << 16) |
                       ((uint32_t)data[1] << 8)  |
                       (uint32_t)data[2]) & 0x3FFFF;
  return HAL_OK;
}

uint8_t Get_Fifo_left(I2C_HandleTypeDef *hi2c)
{
  uint8_t available = 0;
  uint8_t write_position = 0;
  uint8_t read_position = 0;
  HAL_I2C_Mem_Read(hi2c, MAX30102_ADDR_HAL,
                           MAX30102_REG_FIFO_WR_PTR,
                           I2C_MEMADD_SIZE_8BIT,
                           &write_position,
                           1,
                           100);
  HAL_I2C_Mem_Read(hi2c, MAX30102_ADDR_HAL,
                           MAX30102_REG_FIFO_RD_PTR,
                           I2C_MEMADD_SIZE_8BIT,
                           &read_position,
                           1,
                           100);
  write_position &= 0x1F;
  read_position &= 0x1F;

  if (write_position >= read_position) {
    available = write_position - read_position;
  }
  else {
    available = write_position + 32 - read_position;
  }
  return available;
}

void MAX30102_BandPass_Init(bandpass *bandpass_Hz)
{
  /*
  采样频率为100Hz 0.5-4
  b0 = 0.099424, b1 = 0.000000, b2 = -0.099424
  a0 = 1.000000, a1 = -1.794016, a2 = 0.801151
 */
  bandpass_Hz->a1 = -1.794016;
  bandpass_Hz->a2 = 0.801151;
  bandpass_Hz->b0 = 0.099424;
  bandpass_Hz->b1 = 0.0;
  bandpass_Hz->b2 = -0.099424;
  bandpass_Hz->x1 = 0;
  bandpass_Hz->x2 = 0;
  bandpass_Hz->y1 = 0;
  bandpass_Hz->y2 = 0;
}

float MAX30102_bandpass_filter(float current_value,bandpass *bandpass_Hz)
{
  float y = bandpass_Hz->b0 * current_value +
            bandpass_Hz->b1 * bandpass_Hz->x1 +
            bandpass_Hz->b2 * bandpass_Hz->x2 -
            bandpass_Hz->a1 * bandpass_Hz->y1 -
            bandpass_Hz->a2 * bandpass_Hz->y2;
  bandpass_Hz->x2 = bandpass_Hz->x1;
  bandpass_Hz->x1 = current_value;
  bandpass_Hz->y2 = bandpass_Hz->y1;
  bandpass_Hz->y1 = y;
  return y;
}

void Slide_Window_Init(heart_buff_t* hb)
{
  hb->count = 0;
  hb->head = 0;
  for (uint16_t i = 0;i<SAVE_HEART_DATA_SIZE;i++) {
    hb->data[i] = 0;
  }
}

void Slide_Window_AddData(heart_buff_t* hb,int newvalue)
{
  hb->data[hb->head] = newvalue;
  hb->head = (hb->head + 1) % SAVE_HEART_DATA_SIZE;
  if (hb->count < SAVE_HEART_DATA_SIZE) {
    hb->count++;
  }
}

int Slide_Window_GetData(heart_buff_t *hb, int index)
{
  if (index < 0 || index >= hb->count) {
    return 0;
  }
  int true_index;
  // 如果环形缓冲区还没填满，index是多少就是多少
  if (hb->count < SAVE_HEART_DATA_SIZE) {
    true_index = index;
  } // 映射到真实的索引上
  else {
     true_index = (hb->head + index)% SAVE_HEART_DATA_SIZE;
  }
  return hb->data[true_index];
}

