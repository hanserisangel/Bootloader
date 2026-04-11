#ifndef __AT24C64_H__
#define __AT24C64_H__

#include <stdint.h>

#define AT24C64_ADDRESS 0xA0  // AT24C64的I2C地址
#define AT24C64_PAGE_SIZE 32 // 每页32字节

void AT24C64_Init(void);
void AT24C64_WritePage(uint16_t MemAddress, uint8_t *pData, uint16_t Length);
void AT24C64_ReadPage(uint16_t MemAddress, uint8_t *pData, uint16_t Length);
void AT24C64_ReadOTAInfo(void);
void AT24C64_WriteOTAInfo(void);

#endif /* __AT24C64_H__ */
