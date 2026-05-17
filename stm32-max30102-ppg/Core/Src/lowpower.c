#include "lowpower.h"
#include "main.h"
#include "FreeRTOS.h"
#include "max30102.h"
#include "task.h"
#include "OLED.h"

extern HAL_StatusTypeDef MAX30102_ShutDown(I2C_HandleTypeDef *hi2c);
extern I2C_HandleTypeDef hi2c1;
extern void SystemClock_Config(void);

volatile uint8_t g_stop_mode = 0;

static void OELD_OFF(void)
{
  // 显示关闭
  OLED_WriteCommand(0xAE);
  // 电荷泵关闭
  OLED_WriteCommand(0x8D);
  OLED_WriteCommand(0x10);
}

static void OELD_ON(void)
{
  // 电荷泵开启
  OLED_WriteCommand(0x8D);
  // 显示开启
  OLED_WriteCommand(0x14);
  OLED_WriteCommand(0xAF);
}


void ESP32_Stop_Upload(void){
  HAL_GPIO_WritePin(GPIOF,GPIO_PIN_0,GPIO_PIN_RESET);
}
void ESP32_Start_Upload(void){
  HAL_GPIO_WritePin(GPIOF,GPIO_PIN_0,GPIO_PIN_SET);
}

void enter_lowpowermode()
{
  OLED_ShowString(4,5,"down"); // OK
  OELD_OFF();
  MAX30102_ShutDown(&hi2c1);  // OK
  ESP32_Stop_Upload();
  g_stop_mode = 1;
  HAL_SuspendTick();
  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  // 唤醒后从下面执行
  SystemClock_Config();
  HAL_ResumeTick();
  g_stop_mode = 0;
  ESP32_Start_Upload();
  OELD_ON();
  MAX30102_Init(&hi2c1,0);
  vTaskDelay(pdMS_TO_TICKS(2000));
}


