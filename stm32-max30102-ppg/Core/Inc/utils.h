#ifndef INVICIBLE_CRC16_H
#define INVICIBLE_CRC16_H

#include "stdint.h"
uint16_t calc_crc16_uint16(const uint8_t *data,uint16_t length);
void vApplicationTickHook(void);

#endif //INVICIBLE_CRC16_H
