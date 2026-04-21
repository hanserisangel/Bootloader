#include "Ymodem.h"
#include "main.h"
#include <stdlib.h>

uint16_t Ymodem_CRC16(uint8_t *pdata, uint16_t length)
{
    uint16_t CrcInit = 0x0000;
    uint16_t CrcPoly = 0x1021;

    while(length --)
    {
        CrcInit = (*pdata ++ << 8) ^ CrcInit;

        for(uint8_t i = 0; i < 8; i ++)
        {
            if(CrcInit & 0x8000) CrcInit = (CrcInit << 1) ^ CrcPoly;
            else CrcInit = (CrcInit << 1);
        }
    }

    return CrcInit;
}

/**
 * @brief  Ymodem 数据包校验函数，验证数据包的合法性
 * @param  packet: 指向接收到的数据包的指针
 * @param  size: 数据包的总长度
 * @param  data_len: 输出参数，返回数据包中有效数据的长度
 * @param  block_num: 输出参数，返回数据包中的块号
 * @retval bool: true, false
 */
bool Ymodem_CheckPacket(const uint8_t *packet, uint16_t size, uint16_t *data_len, uint8_t *block_num)
{
    if(size < 5)
        return false;
    uint16_t expected_len = 0;
    if(packet[0] == SOH)        // SOH: 128 字节数据包
        expected_len = 128;
    else if(packet[0] == STX)   // STX: 1024 字节数据包
        expected_len = 1024;
    else
        return false;

    if(size != (uint16_t)(expected_len + 5))    // 数据包总长度应该是数据长度 + 5 (1 字节头 + 1 字节块号 + 1 字节块号补码 + 2 字节 CRC)
        return false;

    if((uint8_t)(packet[1] + packet[2]) != 0xFF)    // 块号和块号补码应该加起来等于 0xFF
        return false;

    {   // CRC 校验
        uint16_t received_crc = ((uint16_t)packet[3 + expected_len] << 8) | packet[3 + expected_len + 1];
        uint16_t calc_crc = Ymodem_CRC16((uint8_t *)(packet + 3), expected_len);
        if(received_crc != calc_crc)
            return false;
    }

    *data_len = expected_len;
    *block_num = packet[1];
    return true;
}

/**
 * @brief  Ymodem 头包解析函数，从头包中提取文件大小等信息
 * @param  data: 指向头包数据的指针
 * @param  data_len: 头包数据的长度
 * @param  file_size: 输出参数，返回文件大小
 * @param  is_end: 输出参数，返回是否是结束包（没有文件名和大小）
 * @retval bool: true, false
 */
bool Ymodem_ParseHeader(const uint8_t *data, uint16_t data_len, uint32_t *file_size, bool *is_end)
{
    const char *name = (const char *)data;      // 头包中的文件名以 null 结尾，紧跟在数据开始的位置
    size_t name_len = 0;
    while((name_len < data_len) && (name[name_len] != '\0'))    // 文件名以 null 结尾
        name_len ++;
    if(name_len == 0)   // 没有文件名，说明是结束包
    {
        *is_end = true;
        *file_size = 0;
        return true;
    }

    if(name_len + 1 >= data_len)    // 没有文件大小信息
        return false;

    const char *size_str = name + name_len + 1;     // 文件大小字符串紧跟在文件名后面，以 null 结尾
    if(size_str >= (const char *)(data + data_len))
        return false;

    *file_size = (uint32_t)strtoul(size_str, NULL, 10);
    *is_end = false;        // 头包不可能是结束包
    return true;
}

/**
 * @brief  Ymodem 数据块写入函数
 * @param  block_index: 数据块的索引，从 0 开始
 * @param  buf: 指向数据块内容的指针
 * @param  size: 数据块的大小，单位字节
 * @retval None
 */
void Ymodem_WriteBlock(uint32_t block_index, const uint8_t *buf, uint32_t size)
{
    if(size == 0)
        return;

    W25Q64_WriteBytes(OTA_STAGING_ADDR + block_index * UPDATA_BUFF, buf, size);
}

/**
 * @brief  Ymodem 传输完成处理函数，进行必要的收尾工作，如写入剩余数据、记录文件大小、清除 OTA 标志等
 * @param  start_time: 传输开始的时间戳，用于计算传输耗时
 * @retval None
 */
void Ymodem_Finalize(uint32_t g_firmware_size)
{
    if(UpData_A.Ymodem_BytesInBuffer > 0)   // 如果还有剩余数据没有写入，先写入剩余数据
    {
        Ymodem_WriteBlock(UpData_A.Ymodem_WriteBlockIndex,
            UpData_A.UpAppBuffer, UpData_A.Ymodem_BytesInBuffer);
    }
    if(OTA_Info.FileSize == 0)
        OTA_Info.FileSize = UpData_A.Ymodem_TotalReceived;

    if(!Boot_VerifySignature()) // 签名验证失败，说明固件不合法，不能更新
    {
        LOG_E("Signature verify failed");
        OTA_Info.FileSize = 0;
        return;
    }

    HAL_Delay(500);    // 等待写入完成
    OTA_Info.FileSize = g_firmware_size;
    W25Q64_WriteOTAInfo();
    
    LOG_I("Download A program OK!");
}
