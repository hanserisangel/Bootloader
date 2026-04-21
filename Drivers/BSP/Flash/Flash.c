#include "main.h"

/**
 * @brief  擦除指定的 Flash 扇区。
 * @param  sector: 起始扇区号。0~11 对应扇区0~11。
 * @param  count: 要擦除的扇区数量。
 * @retval None
 */
void MCU_EraseFlash(uint8_t sector, uint8_t count)
{
    // 解锁 Flash 控制寄存器
    HAL_FLASH_Unlock();
    
    // 配置擦除参数
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    
    EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector        = FLASH_SECTOR_0 + sector; // 根据起始扇区号计算实际扇区
    EraseInitStruct.NbSectors     = count; // 擦除的扇区数量
    
    // 执行擦除操作
    if(HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK)
    {
        LOG_E("MCU_EraseFlash failed, sector=%u count=%u err=%lu", sector, count, SectorError);
    }
    
    // 锁定 Flash 控制寄存器
    HAL_FLASH_Lock();
}

/**
 * @brief  向 Flash 写入数据。
 * @param  WriteAddress: 写入的起始地址。
 * @param  pData: 指向要写入数据的指针。
 * @param  Length: 要写入的数据长度（以字为单位）。
 * @retval None
 */
void MCU_WriteFlash(uint32_t WriteAddress, uint32_t *pData, uint32_t Length)
{
    // 解锁 Flash 控制寄存器
    HAL_FLASH_Unlock();
    
    // 写入数据
    for (uint32_t i = 0; i < Length; i++)
    {
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, WriteAddress + i*4U, pData[i]) != HAL_OK)
        {
            LOG_E("MCU_WriteFlash failed, addr=0x%08lX idx=%lu", WriteAddress + i*4U, i);
            break;
        }
    }
    
    // 锁定 Flash 控制寄存器
    HAL_FLASH_Lock();
}

/**
 * @brief  读取指定地址的 Flash 数据。
 * @param  ReadAddress: 读取地址。
 * @param  pData: 数据缓冲区指针。
 * @param  Length: 读取数据的长度（单位：字）。
 * @retval None
 */
void MCU_ReadFlash(uint32_t ReadAddress, uint32_t *pData, uint32_t Length)
{
    for (uint32_t i = 0; i < Length; i++)
    {
        pData[i] = *(__IO uint32_t *)(ReadAddress + i*4);
    }
}
