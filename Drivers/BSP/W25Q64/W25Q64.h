#ifndef __W25Q64_H__
#define __W25Q64_H__

#include <stdint.h>

#define W25Q64_PAGE_SIZE       256
#define W25Q64_CS_PIN        GPIO_PIN_3
#define W25Q64_CS_GPIO_PORT  GPIOG

void W25Q64_Init(void);
void W25Q64_ReadData(uint16_t PageNum, uint8_t* pData, uint32_t Size);
void W25Q64_PageProgram(uint16_t PageNum, uint8_t* pData, uint32_t Size);
void W25Q64_EraseBlock(uint8_t BlockNum);

#endif // __W25Q64_H__
