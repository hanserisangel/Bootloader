#ifndef __FLASH_H__
#define __FLASH_H__

#include <stdint.h>

void MCU_EraseFlash(uint8_t sector, uint8_t count);
void MCU_WriteFlash(uint32_t WriteAddress, uint32_t *pData, uint32_t Length);
void MCU_ReadFlash(uint32_t ReadAddress, uint32_t *pData, uint32_t Length);

#endif /* __FLASH_H__ */
