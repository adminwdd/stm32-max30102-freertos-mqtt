/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "event_groups.h"
#include "max30102.h"
#include "OLED.h"
#include "queue.h"
#include <string.h>
#include "utils.h"
#include <stdio.h>
#include "lowpower.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

// 内存不对齐__attribute__packed
// sizeof 444Byte
typedef struct __attribute__((packed)){
  int32_t ppg[100];
  uint16_t heartrate;
  uint16_t valley_count; //峰谷的数量
  uint16_t valley_index[20]; // 心率最高240时最多4个波谷，存储20个峰谷数据，盈余一些
} Trans_Data_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_PAYLOAD_LEN  sizeof(Trans_Data_t)

#define STARTBYTE1 0xAA
#define STARTBYTE2 0x55

#define ENDBYTE1 0X0D
#define ENDBYTE2 0X0D

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern I2C_HandleTypeDef hi2c1;

Trans_Data_t trans_data_t ; // 位于内存中的.bss自动初始化为0

// UART要发送的buffer
uint8_t txbuf[UART_PAYLOAD_LEN + 10] = {0};

// 任务创建返回值
BaseType_t xCreateResult ;

// 任务句柄
TaskHandle_t xMax30102TaskHandle  = NULL;
TaskHandle_t xPpgProcessTaskHandle  = NULL;
TaskHandle_t xOledTaskHandle  = NULL;
TaskHandle_t xUploadHandle = NULL;
TaskHandle_t xLowPowerTaskHandle  = NULL;


// 事件组句柄
EventGroupHandle_t xStatusEventHandle = NULL;

// 用于OLED显示的心率值
uint16_t hr = 0;

// 心率队列Static
QueueHandle_t HeartRateHandle;
uint8_t QueueStorageBuffer[160];
StaticQueue_t xQueueBuffer;

// 传输静态队列Static
QueueHandle_t TransferHandle;
uint8_t QueueTransferBuffer[sizeof(Trans_Data_t)*4];
StaticQueue_t xQueueTransferBuffer;

// 事件组Static
StaticEventGroup_t xEventGroupBuffer;

// 事件
#define MAX30102_Init_Failed 				          (1UL << 0)
#define MAX30102_REG_INTR_STATUS_1_Failed 	  (1UL << 1)
#define MAX30102_REG_INTR_STATUS_2_Failed 	  (1UL << 2)
#define MAX30102_GetDataPointer_Failed 	      (1UL << 3)
#define MAX30102_QUEUE_FULL_ERR          	    (1UL << 4)
#define DMA_START_FAILED                      (1UL << 5)
#define DMA_TIME_OUT                          (1UL << 6)
#define ALL_Bit_Error (MAX30102_Init_Failed|MAX30102_REG_INTR_STATUS_1_Failed|\
                      MAX30102_REG_INTR_STATUS_2_Failed|MAX30102_GetDataPointer_Failed|\
                      MAX30102_QUEUE_FULL_ERR|DMA_START_FAILED|\
                      DMA_TIME_OUT)

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Task_MAX30102(void *argument);
void Task_PPG_Process(void *argument);
void Task_OLED_Show(void *argument);
void Task_Upload(void *argument);
void Is_Low_Power(void *argument);

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);
void UART_Protocol_EncodeFrame(Trans_Data_t *data,uint16_t length);
void UART_Protocol_Init(void);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationTickHook(void);

/* USER CODE BEGIN 3 */
volatile uint8_t g_ucCPUUsage = 0;
void vApplicationTickHook( void )
{
  static uint32_t ulIdleTicks = 0;
  static uint32_t ulTotalTicks = 0;
  // 获取空闲任务的句柄（系统启动后只会获取一次）
  static TaskHandle_t xIdleTaskHandle = NULL;
  if (xIdleTaskHandle == NULL) {
    xIdleTaskHandle = xTaskGetIdleTaskHandle();
  }
  // 统计总次数
  ulTotalTicks++;
  //  检查当前正在运行的任务是否是空闲任务
  if (xTaskGetCurrentTaskHandle() == xIdleTaskHandle) {
    ulIdleTicks++;
  }
  // 100个Tick后计算CPU使用率
  if (ulTotalTicks >= 100) {
    // 计算百分比：100 - (空闲比例 * 100)
    g_ucCPUUsage = 100 - (100 * ulIdleTicks) / ulTotalTicks;
    ulIdleTicks = 0;
    ulTotalTicks = 0;
  }
}
/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
    // 创建事件组
    xStatusEventHandle = xEventGroupCreateStatic(&xEventGroupBuffer);
    if (xStatusEventHandle == NULL)
    {
      Error_Handler();
    }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  // 创建队列,队列长度20,每个元素4字节，存储在QueueStorageBuffer
  HeartRateHandle = xQueueCreateStatic(40,4,QueueStorageBuffer,&xQueueBuffer);
  xQueueReset(HeartRateHandle);

  // 创建队列,队列长度4,每个元素sizeof(Trans_Data_t)字节，存储在QueueTransferBuffer
  TransferHandle = xQueueCreateStatic(4,sizeof(Trans_Data_t),QueueTransferBuffer,&xQueueTransferBuffer);
  xQueueReset(TransferHandle);

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */

  xCreateResult = xTaskCreate(
    Task_MAX30102,
    "Task_MAX30102",
    256,
    NULL,
    10,
    &xMax30102TaskHandle
  );
  if (xCreateResult != pdPASS) {
    Error_Handler();
  }

   xCreateResult = xTaskCreate(
       Task_PPG_Process,
       "Task_PPG_Process",
       512,
       NULL,
       9,
       &xPpgProcessTaskHandle
   );
  if (xCreateResult != pdPASS) {
    Error_Handler();
  }

  xCreateResult = xTaskCreate(
      Task_OLED_Show,
      "Task_OLED_Show",
      256,
      (void *)&hr,
      7,
      &xOledTaskHandle
  );
  if (xCreateResult != pdPASS) {
    Error_Handler();
  }

  xCreateResult = xTaskCreate(
      Task_Upload,
      "Task_Upload",
      512,
      NULL,
      8,
      &xUploadHandle
    );
  if (xCreateResult != pdPASS) {
    Error_Handler();
  }

  xCreateResult = xTaskCreate(
    Is_Low_Power,
    "Is_Low_Power",
    256,
    NULL,
    11,
    &xLowPowerTaskHandle
  );
  if (xCreateResult != pdPASS) {
    Error_Handler();
  }

  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void Task_MAX30102(void *argument)
{
  // 若MAX30102_Init失败则记录到事件组中
  if (MAX30102_Init(&hi2c1,0) != HAL_OK)
  {
    xEventGroupSetBits(xStatusEventHandle,MAX30102_Init_Failed);
  }
  uint32_t FIFOdata = 0;
  uint8_t status1 = 0;
  uint8_t status2 = 0;
  // FIFO可读取的数据
  while (1)
  {
    // 任务通知接收，采用率是100Hz,最多等1s
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    // 读状态寄存器清除中断
    if (MAX30102_ReadReg(&hi2c1, MAX30102_REG_INTR_STATUS_1, &status1) != HAL_OK)
    {
      xEventGroupSetBits(xStatusEventHandle,MAX30102_REG_INTR_STATUS_1_Failed);
    }
    if (MAX30102_ReadReg(&hi2c1, MAX30102_REG_INTR_STATUS_2, &status2) != HAL_OK)
    {
      xEventGroupSetBits(xStatusEventHandle,MAX30102_REG_INTR_STATUS_2_Failed);
    }
    // 获取FIFO可读取的数据
    if (MAX30102_GetDataPointer(&hi2c1,&FIFOdata) != HAL_OK) {
      xEventGroupSetBits(xStatusEventHandle,MAX30102_GetDataPointer_Failed);
    }
    // 发到队列上
    if (xQueueSendToBack(HeartRateHandle,&FIFOdata,0) != pdPASS) {
      xEventGroupSetBits(xStatusEventHandle,MAX30102_QUEUE_FULL_ERR);
    }
  }
}

void Task_PPG_Process(void *argument)
{
  // 队列接收状态
  BaseType_t QueueReceiveStatus = pdFALSE;

  // 滤波器初始化
  bandpass bandpass_30102;
  MAX30102_BandPass_Init(&bandpass_30102);

  // 环形缓冲区接收数据
  static heart_buff_t hb_buffer;
  heart_buff_t* hb = &hb_buffer;
  Slide_Window_Init(hb);

  // 创建一个临时变量，存储传感器读到的数据
  uint32_t ir_value = 0;
  // 记录是否获取到最新的100点数据
  static uint8_t step_count = 99; // 环形缓冲区是空的，当环形缓冲区满了，计算一次
  // 记录波形最大最小值
  static long max_wave = 0;
  static long min_wave = 0;
  // 结构体ppg索引
  uint8_t ppg_t_index = 0;
  while (1)
  {
     QueueReceiveStatus = xQueueReceive(HeartRateHandle,&ir_value,portMAX_DELAY);
    // 读到队列的数据了
    if (QueueReceiveStatus == pdPASS) {

      float get_value = MAX30102_bandpass_filter((float)ir_value,&bandpass_30102);
      // 保留两位小数，截段剩余部分
      int get_value_100 = (int)(get_value * 100.0f);
      // 加入环形缓冲区
      Slide_Window_AddData(hb,get_value_100);
      // 当环形缓冲区满时，开始处理数据
      if (hb->count == 500) step_count++;
      // 如果更新的数据为100时，计算一次心率
      if (step_count >= 100) {
        // 更新计数值为0
        step_count = 0;
        // 初始化波形最大最小值
        min_wave = Slide_Window_GetData(hb, 0);
        max_wave = min_wave;
        // 遍历环形缓冲区，找出最大最小值
        for (int i = 1; i < 500; i++) {
          int v = Slide_Window_GetData(hb, i);
          if (v < min_wave) min_wave = v;
          if (v > max_wave) max_wave = v;
        }
        // 阈值经验公式
        long threshold = min_wave + (long)(0.45f * (max_wave - min_wave));

        // hb_index 用于存储峰谷索引值，采样率100HZ
        // hb_count 用于记录存储了多少峰值
        // bottom_index 更新峰谷索引
        uint16_t hb_index[20];
        uint8_t hb_count = 0;
        int bottom_index = -1;

        // 遍历环形缓冲区，寻找峰谷，找5点中最小值
        int p_2 = Slide_Window_GetData(hb, 0);
        int p_1 = Slide_Window_GetData(hb, 1);
        int p_0 = Slide_Window_GetData(hb, 2);
        int p1  = Slide_Window_GetData(hb, 3);
        int p2;
        for (int i = 2; i < 500-2; i++) {
          p2 = Slide_Window_GetData(hb, i+2);
        // p0为五点最小且p0小于阈值，则p0为峰谷点
        if (p_0 < p_1 && p_0 < p1 && p_1 < p_2 && p1 < p2 && p_0 < threshold) {
          // if (bottom_index == -1 ||( i - bottom_index >=25 && i-bottom_index < 200))
          // 测静息心率来说的话，心率在50到150之间
          if (bottom_index == -1 ||(i - bottom_index >=40 && i - bottom_index <= 120) )
            {
            if (hb_count < 20) {
              // 记录峰谷索引
              hb_index[hb_count++] = i;
              // 更新峰谷索引
              bottom_index = i;
            }
          }
        }
          p_2 = p_1;
          p_1 = p_0;
          p_0 = p1;
          p1  = p2;
        }

        // 装填前清理trans_data_t
        memset(&trans_data_t, 0, sizeof(trans_data_t));
        // 装填数据到结构体
        for (uint16_t i = 0;i<100;i++) {
          trans_data_t.ppg[i] = Slide_Window_GetData(hb,i+400);
        }
        trans_data_t.heartrate = 0;
        for (int i = 0; i < hb_count; i++) {
          trans_data_t.valley_index[i] = hb_index[i];
        }
        trans_data_t.valley_count = hb_count;
        // 计算心率值
        uint16_t average_heart = 0;
        for (int i = 1; i < hb_count; i++) {
          average_heart += hb_index[i] - hb_index[i-1];
        }
        if (hb_count < 2) {
          xQueueSendToBack(TransferHandle,&trans_data_t,0);
          continue;
        }
        average_heart /= (hb_count - 1);
        if (average_heart == 0) {
          xQueueSendToBack(TransferHandle,&trans_data_t,0);
          continue;
        }
        hr = 6000/(average_heart);
        trans_data_t.heartrate = hr;
        // 队列发送数据
        xQueueSendToBack(TransferHandle,&trans_data_t,0);
      }
    }
  }
}

static void OLED_ShowError(EventBits_t error_bits)
{
  if (error_bits & MAX30102_Init_Failed)
  {
    OLED_ShowString(4, 1, "ERR:MAX INIT   ");
  }
  else if (error_bits & MAX30102_REG_INTR_STATUS_1_Failed)
  {
    OLED_ShowString(4, 1, "ERR:INT ST1    ");
  }
  else if (error_bits & MAX30102_REG_INTR_STATUS_2_Failed)
  {
    OLED_ShowString(4, 1, "ERR:INT ST2    ");
  }
  else if (error_bits & MAX30102_GetDataPointer_Failed)
  {
    OLED_ShowString(4, 1, "ERR:FIFO PTR   ");
  }
  else if (error_bits & MAX30102_QUEUE_FULL_ERR)
  {
    OLED_ShowString(4, 1, "ERR:HR QUEUE   ");
  }
  else if (error_bits & DMA_START_FAILED)
  {
    OLED_ShowString(4, 1, "ERR:DMA START  ");
  }
  else if (error_bits & DMA_TIME_OUT)
  {
    OLED_ShowString(4, 1, "ERR:DMA TIMEOUT");
  }
  else
  {
    OLED_ShowString(4, 1, "OK             ");
  }
}

void Task_OLED_Show(void *argument)
{
  // 心率值指针
  volatile uint16_t *hr_ptr = (volatile uint16_t *)argument;
  // 进入中断的次数
  extern volatile uint32_t g_max30102IntCount;
  // cpu占用率
  // extern uint8_t g_ucCPUUsage;
  // 固定1s
  const TickType_t refresh_period = pdMS_TO_TICKS(1000);

  TickType_t last_wake = xTaskGetTickCount();
  TickType_t last_tick = last_wake;

  uint32_t last_int = g_max30102IntCount ;
  uint32_t irq_rate = 0;

  OLED_ShowString(1, 1, "HR:");
  OLED_ShowString(2, 1, "IRQ:");
  OLED_ShowString(3, 1, "CPU:");
  OLED_ShowString(3, 8, "%");
  OLED_ShowString(4,1,"OK");

  while (1)
  {
    TickType_t now = xTaskGetTickCount();

    uint32_t now_int = g_max30102IntCount;
    uint32_t delta_int = now_int - last_int;
    TickType_t delta_tick = now - last_tick;

    // EventBits_t EventGroupStatus = xEventGroupWaitBits(xStatusEventHandle,ALL_Bit_Error,pdFALSE,pdFALSE,0);
    EventBits_t EventGroupStatus = xEventGroupGetBits(xStatusEventHandle);
    EventGroupStatus &= ALL_Bit_Error;
    OLED_ShowError(EventGroupStatus);

    if (delta_tick > 0)
    {
      irq_rate = (uint32_t)((delta_int * 1000) / ( delta_tick ));
    }
    last_int = now_int;
    last_tick = now;

    OLED_ShowNum(1, 5, *hr_ptr, 3);
    OLED_ShowNum(2, 5, irq_rate, 5);
    OLED_ShowNum(3, 5, g_ucCPUUsage, 3);

    vTaskDelayUntil(&last_wake, refresh_period);
  }
}



void Task_Upload(void *argument)
{
  extern UART_HandleTypeDef huart4;
  extern UART_HandleTypeDef huart1;
  UART_Protocol_Init();
  static Trans_Data_t receiver;
  HAL_StatusTypeDef uart_t_status;

  while (1) {
    // 如果队列有数据来了
    if (xQueueReceive(TransferHandle, &receiver, portMAX_DELAY) == pdPASS) {

      // 对接收到的数据进行协议帧编码，添加帧头、序号、长度、CRC校验和帧尾
      UART_Protocol_EncodeFrame(&receiver,UART_PAYLOAD_LEN);

      // 等待上一次DMA发送完成的通知（非阻塞，立即返回）
      ulTaskNotifyTake(pdTRUE, 0);

      // 通过UART4使用DMA方式发送编码后的数据帧（数据长度 = 有效载荷 + 10字节帧开销）
      HAL_StatusTypeDef dmatask = HAL_UART_Transmit_DMA(&huart4,txbuf,UART_PAYLOAD_LEN + 10);

      // 用于测试
      // HAL_UART_Transmit(&huart1,txbuf,UART_PAYLOAD_LEN + 10,100);

      if (dmatask == HAL_OK) {
        // 等待100ms，如果100ms没人叫我，返回0,有人叫我的话，返回值大于1
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0)
        {
          xEventGroupSetBits(xStatusEventHandle,DMA_TIME_OUT);
          HAL_UART_AbortTransmit(&huart4);
        }
      }
      else
      {
        xEventGroupSetBits(xStatusEventHandle,DMA_START_FAILED);
      }

    }
 }
}

// tx_buf除了序号,数据和crc均load
void UART_Protocol_Init(void)
{
  // 填充帧头起始标志 (0xAA 0x55)，用于标识数据帧的开始
  txbuf[0] = STARTBYTE1;
  txbuf[1] = STARTBYTE2;

  // 填充有效载荷长度字段（小端模式），位于帧的第4-5字节
  txbuf[4] = UART_PAYLOAD_LEN & 0xFF;        // 长度低8位
  txbuf[5] = (UART_PAYLOAD_LEN >> 8) & 0xFF; // 长度高8位

  // 填充帧尾结束标志 (0x0D 0x0D)，用于标识数据帧的结束
  // 位置在有效载荷数据之后（偏移量为 UART_PAYLOAD_LEN + 8）
  txbuf[UART_PAYLOAD_LEN + 8] = ENDBYTE1;
  txbuf[UART_PAYLOAD_LEN + 9] = ENDBYTE2;
}

 // 帧格式：[帧头2B][序号2B][长度2B][数据N 444B][CRC 2B][帧尾2B]
void UART_Protocol_EncodeFrame(Trans_Data_t *data,uint16_t length)
{
  // 静态变量：帧序列号，每次发送自动递增（0-65535循环）
  static uint16_t S_number = 0;

  // 填充帧序号（小端模式），位于帧的第2-3字节
  txbuf[2] = S_number & 0xFF;        // 序号低8位
  txbuf[3] = (S_number >> 8) & 0xFF; // 序号高8位
  S_number++; // 序号自增，用于接收方检测丢包和帧顺序

  // 将用户数据复制到帧的数据区域（从偏移量6开始）
  // 数据结构：ppg[100](444字节) + heartrate(2字节) = 446字节
  memcpy(&txbuf[6],data,length);

  // 计算CRC16校验码，校验范围包含：序号(2B) + 长度(2B) + 数据(NB)
  // 从txbuf[2]开始，长度为 length + 4（4 = 2字节序号 + 2字节长度）
  uint16_t crc = calc_crc16_uint16(&txbuf[2],length + 4);      // 校验序号+长度+数据

  // 填充CRC校验码到帧中（小端模式），位置在数据之后
  txbuf[UART_PAYLOAD_LEN + 6] = crc & 0xFF;        // CRC低8位
  txbuf[UART_PAYLOAD_LEN + 7] = (crc >> 8 ) & 0xFF; // CRC高8位
}

// DMA发送完成回调函数，触发任务完成通知
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xUploadHandle , &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}


void Is_Low_Power(void *argument)
{
  while (1) {
    // 等待任务通知，清楚通知值，
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET) {
      // 检测到低电平，进入低功耗模式
      extern void enter_lowpowermode();
      enter_lowpowermode();
    }
    while (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_RESET)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}



/* USER CODE END Application */

