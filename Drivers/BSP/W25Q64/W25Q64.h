#ifndef __W25Q64_H__
#define __W25Q64_H__

#include <stdint.h>

#define W25Q64_PAGE_SIZE       256
#define W25Q64_SECTOR_SIZE     (4 * 1024) // 4KB
#define W25Q64_BLOCK_SIZE      (64 * 1024) // 64KB
#define W25Q64_TOTAL_SIZE      (8 * 1024 * 1024)
#define W25Q64_CS_PIN        GPIO_PIN_3
#define W25Q64_CS_GPIO_PORT  GPIOG

void W25Q64_Init(void);
void W25Q64_ReadData(uint16_t PageNum, uint8_t* pData, uint32_t Size);
void W25Q64_PageProgram(uint16_t PageNum, uint8_t* pData, uint32_t Size);
void W25Q64_EraseBlock(uint8_t BlockNum);
void W25Q64_ReadBytes(uint32_t Address, uint8_t* pData, uint32_t Size);
void W25Q64_WriteBytes(uint32_t Address, const uint8_t* pData, uint32_t Size);
void W25Q64_EraseSector(uint32_t SectorNum);
void W25Q64_ReadOTAInfo(void);
void W25Q64_WriteOTAInfo(void);

#endif // __W25Q64_H__
