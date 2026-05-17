#include "utils.h"
#include "FreeRTOS.h"
#include "task.h"


uint16_t calc_crc16_uint16(const uint8_t *data,uint16_t length)
{
  uint16_t crc = 0xffff;
  for (uint16_t i = 0;i<length;i++) {
    crc ^= data[i];
    for (uint8_t j = 0;j<8;j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xa001;
      }
      else {
        crc >>= 1;
      }
    }
  }
  return crc;
}


// volatile uint8_t g_ucCPUUsage = 0;
// // 钩子函数：由系统在每个 Tick 中断中自动调用
// void vApplicationTickHook(void) {
//   static uint32_t ulIdleTicks = 0;
//   static uint32_t ulTotalTicks = 0;
//   // 获取空闲任务的句柄（系统启动后只会获取一次）
//   static TaskHandle_t xIdleTaskHandle = NULL;
//   if (xIdleTaskHandle == NULL) {
//     xIdleTaskHandle = xTaskGetIdleTaskHandle();
//   }
//   // 统计总次数
//   ulTotalTicks++;
//   //  检查当前正在运行的任务是否是空闲任务
//   if (xTaskGetCurrentTaskHandle() == xIdleTaskHandle) {
//     ulIdleTicks++;
//   }
//   // 100个Tick后计算CPU使用率
//   if (ulTotalTicks >= 100) {
//     // 计算百分比：100 - (空闲比例 * 100)
//     g_ucCPUUsage = 100 - (100 * ulIdleTicks) / ulTotalTicks;
//     ulIdleTicks = 0;
//     ulTotalTicks = 0;
//   }
// }



