#include "main.h"
#include "i2c.h"
#include "AT24C64.h"

// AT24C64: 64Kb = 8192Bytes, 地址范围0x0000 - 0x1FFF，每页32字节，共有256页

void AT24C64_Init(void)
{
    // 初始化I2C外设
    MX_I2C1_Init();
}

/**
 * @brief  写入数据到 AT24C64。
 * @param  MemAddress: 内存地址，范围0x00 - 0x1FFF。
 * @param  pData: 指向要写入数据的指针。
 * @param  Length: 要写入的数据长度（以字节为单位）。
 * @retval None
 */
void AT24C64_WritePage(uint16_t MemAddress, uint8_t *pData, uint16_t Length)
{
    uint16_t bytesWritten = 0;
    // AT24C64每次写入不能超过32字节，需要分块写入
    while (bytesWritten < Length)
    {
        uint16_t chunkSize = (Length - bytesWritten > AT24C64_PAGE_SIZE) ? AT24C64_PAGE_SIZE : (Length - bytesWritten); // 每次写入32字节
        HAL_I2C_Mem_Write(&hi2c1, AT24C64_ADDRESS, MemAddress + bytesWritten, I2C_MEMADD_SIZE_16BIT, pData + bytesWritten, chunkSize, HAL_MAX_DELAY);
        bytesWritten += chunkSize;
        HAL_Delay(5); // 写入后需要等待EEPROM完成写入操作
    }
}

/**
 * @brief  从 AT24C64 读取数据。
 * @param  MemAddress: 内存地址，范围0x00 - 0x1FFF。
 * @param  pData: 指向存储读取数据的指针。
 * @param  Length: 要读取的数据长度（以字节为单位）。
 * @retval None
 */
void AT24C64_ReadPage(uint16_t MemAddress, uint8_t *pData, uint16_t Length)
{
    HAL_I2C_Mem_Read(&hi2c1, AT24C64_ADDRESS, MemAddress, I2C_MEMADD_SIZE_16BIT, pData, Length, HAL_MAX_DELAY);
}

/**
 * @brief  从 AT24C64 读取 OTA 信息。
 * @retval None
 */
void AT24C64_ReadOTAInfo(void)
{
    memset(&OTA_Info, 0, sizeof(OTA_Info));
    AT24C64_ReadPage(0x0000, (uint8_t *)&OTA_Info, sizeof(OTA_Info));
}

/**
 * @brief  从 AT24C64 写入 OTA 信息。
 * @retval None
 */
void AT24C64_WriteOTAInfo(void)
{
    AT24C64_WritePage(0x0000, (uint8_t *)&OTA_Info, sizeof(OTA_Info));
}
