#include "main.h"
#include "Boot.h"
#include "Ymodem.h"
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "BootOta.h"
#include "BootCrypto.h"
#include "BootDelta.h"

load_a Load_A;
static uint8_t data[RX_DATA_CELLING];
static bool Where_to_store;

static const uint8_t k_ota_ec_pub[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08,
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x35, 0x69, 0xe7, 
    0xab, 0x4b, 0x12, 0x25, 0x12, 0x5d, 0xbe, 0x89, 0x12, 0x7f, 0xb6, 0x2c, 0xcd, 0x7d, 0x91, 
    0xb3, 0xf9, 0x1d, 0x4a, 0x52, 0x11, 0x42, 0x39, 0xad, 0xe8, 0xd9, 0xa6, 0x4a, 0xec, 0xa7, 
    0x45, 0x2a, 0xcf, 0x48, 0xc3, 0x09, 0x3b, 0x4e, 0x06, 0xa3, 0x7b, 0x16, 0x9d, 0xba, 0xe6, 
    0x04, 0x44, 0xf0, 0x93, 0xb6, 0xd3, 0x87, 0x7b, 0x73, 0xe0, 0xa6, 0xf5, 0x27, 0xc4, 0xc5, 0x62
};

// 初级阶段的 Bootloader 设计
static bool BootLoader_Console(uint16_t timeout);
static void BootLoader_Menu(void);
static void BootLoader_Clear(void);
static void LOAD_A(uint32_t addr);
static uint8_t Boot_GetActiveSlot(void);
static uint8_t Boot_GetInactiveSlot(void);
static uint32_t Boot_GetSlotStartAddr(uint8_t slot);
static uint32_t Boot_GetSlotStartSector(uint8_t slot);
static uint32_t Boot_GetSlotSectorCount(uint8_t slot);

static uint8_t Boot_GetActiveSlot(void)
{
    return (OTA_Info.OTA_area == MCU_FLASH_APP_B_SLOT) ? MCU_FLASH_APP_B_SLOT : MCU_FLASH_APP_A_SLOT;
}

static uint8_t Boot_GetInactiveSlot(void)
{
    return (Boot_GetActiveSlot() == MCU_FLASH_APP_A_SLOT) ? MCU_FLASH_APP_B_SLOT : MCU_FLASH_APP_A_SLOT;
}

static uint32_t Boot_GetSlotStartAddr(uint8_t slot)
{
    return (slot == MCU_FLASH_APP_B_SLOT) ? MCU_FLASH_APP_B_ADDR : MCU_FLASH_APP_A_ADDR;
}

static uint32_t Boot_GetSlotStartSector(uint8_t slot)
{
    return (slot == MCU_FLASH_APP_B_SLOT) ? MCU_FLASH_APP_B_SECTOR : MCU_FLASH_APP_A_SECTOR;
}

static uint32_t Boot_GetSlotSectorCount(uint8_t slot)
{
    return (slot == MCU_FLASH_APP_B_SLOT) ? MCU_FLASH_APP_B_COUNT : MCU_FLASH_APP_A_COUNT;
}

/**
 * @brief  Bootloader 主函数，负责引导流程控制
 * @retval None
 */
void BootLoader_Brance(void)
{
    W25Q64_ReadOTAInfo();

    if(BootLoader_Console(20))  // 进入命令行
    {
        LOG_I("Enter console success!");
        BootLoader_Menu();
        OTA_state = UART_CONSOLE_IDLE;
    }
    else // 不进入命令行，直接进行 OTA 跳转功能
    {
        if(OTA_Info.OTA_Flag == OTA_FLAG)
        {
            LOG_I("OTA update from staging");   // OTA 包在外部 staging，解密并写入内部非激活槽

            if(OTA_Info.OTA_type == 0)      // 全量更新
            {
                OTA_state = UPDATA_FULL_SET;
            }
            else if(OTA_Info.OTA_type == 1) // 增量更新
            {
                OTA_state = UPDATA_DELTA_SET;
            }
        }
        else
        {
            uint8_t active_slot = Boot_GetActiveSlot();
            uint8_t inactive_slot = Boot_GetInactiveSlot();

            // A/B双分区自动回滚机制
            if(OTA_Info.OTA_status == FAIL)
            {
                LOG_W("Previous OTA update failed, rollback to previous version");
                active_slot = inactive_slot;
                OTA_Info.OTA_area = active_slot; // 更新 OTA_Info 中的 active slot 信息
                OTA_Info.OTA_status = SUCCESS; // 将状态重置为 SUCCESS，避免重复回滚
            }
            else if(OTA_Info.OTA_status == UPDATE)
            {
                LOG_W("Previous OTA update not verified, skipping to new version");
                OTA_Info.OTA_status = FAIL;
                // 将状态设置为 FAIL，等待本次版本验证结果，如果验证成功会在后续更新为 SUCCESS，如果验证失败则保持 FAIL，等待下次重启回滚
            }

            // 跳转到激活槽的应用程序
            LOG_I("Boot active slot %c", (active_slot == MCU_FLASH_APP_A_SLOT) ? 'A' : 'B');
            LOAD_A(Boot_GetSlotStartAddr(active_slot));
        }
    }
}

/**
 * @brief  菜单显示函数，列出可用的串口命令选项
 * @retval None
 */
static void BootLoader_Menu(void)
{
    Uart_Printf("[1] Erase inactive area\r\n");
    Uart_Printf("[2] Uart IAP download package into mcu\r\n");
    Uart_Printf("[3] Set version number\r\n");
    Uart_Printf("[4] Query version number\r\n");
    Uart_Printf("[5] Download package into W25Q64\r\n");
    Uart_Printf("[6] Download package from W25Q64\r\n");
    Uart_Printf("[7] Reset\r\n");
    Uart_Printf("[8] Apply delta package from W25Q64\r\n");
}

/**
 * @brief  进入串口命令行
 * @param  timeout: 时间限制(单位 100ms)。
 * @retval bool: true, false
 */
static bool BootLoader_Console(uint16_t timeout)
{
    Uart_Printf("if you want to enter console, please input 'w' within %dms\r\n", timeout * 100);
    uint8_t c = 0;
    while(timeout --)
    {
        HAL_Delay(100);
        RingBuffer_Get(uart_rb, &c);
        if(c == 'w') return true; // 进入命令行
    }
    return false; // 不进入命令行
}

/**
 * @brief  串口命令处理函数，根据接收到的命令执行对应的操作
 * @param  pdata: 指向接收到的命令数据的指针
 * @retval None
 */
void BootLoader_Event(uint8_t* pdata)
{
    switch(pdata[0])
    {
        case '1': // 擦除非活动分区
            uint8_t inactive_slot = Boot_GetInactiveSlot();
            MCU_EraseFlash(Boot_GetSlotStartSector(inactive_slot), Boot_GetSlotSectorCount(inactive_slot));
            LOG_I("Erase slot %c OK!", (inactive_slot == MCU_FLASH_APP_A_SLOT) ? 'A' : 'B');
            break;
        case '2': // 串口 IAP 下载固件到 MCU_Flash
            LOG_I("Uart IAP download package into mcu Starting!");
            // MCU_EraseFlash(Boot_GetSlotStartSector(MCU_FLASH_APP_A_SLOT), Boot_GetSlotSectorCount(MCU_FLASH_APP_A_SLOT));

            // 擦除 staging 区，准备接收 OTA 包
            LOG_I("Erase W25Q64 staging block...");
            for(uint32_t addr = OTA_STAGING_ADDR; addr < OTA_STAGING_ADDR + 512U * 1024U; addr += W25Q64_BLOCK_SIZE)
            {
                W25Q64_EraseBlock(addr / W25Q64_BLOCK_SIZE); // 擦除 W25Q64 中存储固件的块
            }
            LOG_I("Erase W25Q64 staging OK!");
            
            OTA_state = IAP_YMODEM_START; // 向 pc 发送申请
            Local_UpDate.Ymodem_Timer = 0;
            Local_UpDate.Ymodem_CRC = 0;
            Local_UpDate.Ymodem_TotalReceived = 0;
            Local_UpDate.Ymodem_WriteBlockIndex = 0;
            Local_UpDate.Ymodem_BytesInBuffer = 0;
            Local_UpDate.Ymodem_ExpectBlock = 0;
            Local_UpDate.Ymodem_HeaderReceived = 0;
            Boot_ResetVerifyState();            // 重置 OTA 验证状态
            Where_to_store = true;              // 设置存储位置为 MCU 的 Flash
            break;
        case '3': // 设置版本号
            LOG_I("Set version number Starting!");
            OTA_state = SET_VERSION;
            break;
        case '4': // 查询版本号
            LOG_I("Query version number Starting!");
            W25Q64_ReadOTAInfo();
            Uart_Printf("version number: %s\r\n", OTA_Info.OTA_version);
            break;
        case '5': // 串口 IAP 下载 A 区程序到 W25Q64
            LOG_I("Download Starting!");

            // 擦除 staging 区，准备接收 OTA 包
            LOG_I("Erase W25Q64 staging block...");
            for(uint32_t addr = OTA_STAGING_ADDR; addr < OTA_STAGING_ADDR + 512U * 1024U; addr += W25Q64_BLOCK_SIZE)
            {
                W25Q64_EraseBlock(addr / W25Q64_BLOCK_SIZE); // 擦除 W25Q64 中存储固件的块
            }
            LOG_I("Erase W25Q64 staging OK!");
            
            OTA_state = IAP_YMODEM_START;
            Local_UpDate.Ymodem_Timer = 0;
            Local_UpDate.Ymodem_CRC = 0;
            Local_UpDate.Ymodem_TotalReceived = 0;
            Local_UpDate.Ymodem_WriteBlockIndex = 0;
            Local_UpDate.Ymodem_BytesInBuffer = 0;
            Local_UpDate.Ymodem_ExpectBlock = 0;
            Local_UpDate.Ymodem_HeaderReceived = 0;
            Boot_ResetVerifyState();
            Where_to_store = false;              // 设置存储位置为 W25Q64
            OTA_Info.FileSize = 0;
            break;
        case '6': // 从 W25Q64 下载程序到 MCU 的 Flash
            LOG_I("Download Starting!");
            OTA_state = UPDATA_FULL_SET;
            break;
        case '7': // 重启
            LOG_I("Reset OK!");
            HAL_Delay(100);
            HAL_NVIC_SystemReset();
            break;
        case '8': // 从 W25Q64 应用 HPatchLite 差分包
            LOG_I("Apply HPatchLite delta Starting!");
            OTA_state = UPDATA_DELTA_SET;
            break;
        default:
            return;
    }
}

/**
 * @brief  Bootloader 状态机函数，根据当前状态处理串口数据
 * @param  None
 * @retval None
 */
void BootLoader_State(void)
{
    uint16_t size;
    int temp;
    size = RingBuffer_Get(uart_rb, data); // 获取串口数据

    // 状态机处理串口数据
    switch(OTA_state)
    {
        case UART_CONSOLE_IDLE:  // 串口命令行
            if(size == 1)  BootLoader_Event(data);
            break;
        case IAP_YMODEM_START:
        {
            uint16_t data_len = 0;
            uint8_t block_num = 0;
            bool is_end = false;

            // 解析头包，获取文件大小等信息
            if(Ymodem_CheckPacket(data, size, &data_len, &block_num) && (block_num == 0))
            {
                uint32_t file_size = 0;
                if(Ymodem_ParseHeader(data + 3, data_len, &file_size, &is_end))
                {
                    if(is_end)
                    {
                        Uart_Printf("\x06"); // ACK
                        OTA_state = UART_CONSOLE_IDLE;
                        break;
                    }

                    OTA_Info.FileSize = file_size;      // 记录文件大小，后续接收数据时用来判断是否接收完成
                    if(OTA_Info.FileSize <= (OTA_HDR_SIZE + OTA_META_LEN))
                    {
                        LOG_E("File size too small for header");
                        Uart_Printf("\x15"); // NAK
                        OTA_state = UART_CONSOLE_IDLE;
                        break;
                    }
                    g_ota.sig_received = 0;
                    g_ota.sig_len = 0;
                    g_ota.firmware_size = 0;
                    g_ota.payload_received = 0;
                    g_ota.hdr_received = 0;
                    g_ota.meta_received = 0;
                    g_ota.sha_started = false;
                    Local_UpDate.Ymodem_HeaderReceived = 1;
                    Local_UpDate.Ymodem_ExpectBlock = 1;

                    Uart_Printf("\x06"); // ACK
                    Uart_Printf("%c", CRC_REQ); // Request data packets
                    OTA_state = IAP_YMODEM_RECEIVED;
                    break;
                }
                Uart_Printf("\x15"); // NAK
            }
            else
            {
                HAL_Delay(10);
                if(Local_UpDate.Ymodem_Timer >= 100)    // 每 1 秒发送一次申请，直到收到头包
                {
                    Uart_Printf("%c", CRC_REQ);
                    Local_UpDate.Ymodem_Timer = 0;
                }
                Local_UpDate.Ymodem_Timer ++;
            }
            break;
        }

        // 数据包解析，并写入 Flash
        case IAP_YMODEM_RECEIVED:
        {
            uint16_t data_len = 0;
            uint8_t block_num = 0;

            if((size == 1) && (data[0] == EOT)) // 传输结束
            {
                Uart_Printf("\x15"); // NAK
                Ymodem_Finalize(g_ota.firmware_size);

                if(Where_to_store)  // 接收完成后自动刷写新固件到mcu flash
                {
                    if(OTA_Info.OTA_type == 0)      // 全量更新
                    {
                        OTA_state = UPDATA_FULL_SET;
                    }
                    else if(OTA_Info.OTA_type == 1) // 增量更新
                    {
                        OTA_state = UPDATA_DELTA_SET;
                    }
                }
                else{
                    OTA_state = UART_CONSOLE_IDLE;
                }
                
                break;
            }

            if(!Ymodem_CheckPacket(data, size, &data_len, &block_num))  // 数据包校验失败
                break;

            if(block_num == Local_UpDate.Ymodem_ExpectBlock)    // 收到期望的数据块
            {
                uint32_t remaining = data_len;  // 计算剩余需要接收的字节数

                // 如果已经知道了文件总大小，就根据文件总大小和已经接收的字节数来计算当前数据块中实际需要处理的字节数，避免处理多余的数据
                if(OTA_Info.FileSize > 0)
                {
                    if(Local_UpDate.Ymodem_TotalReceived >= OTA_Info.FileSize)
                        remaining = 0;
                    else if(Local_UpDate.Ymodem_TotalReceived + data_len > OTA_Info.FileSize)
                        remaining = OTA_Info.FileSize - Local_UpDate.Ymodem_TotalReceived;
                }

                if(remaining > 0)
                {
                    uint32_t copy_len = remaining;      // 实际需要处理的数据长度，一般为1024字节
                    uint32_t payload_offset = 0;        // 已经处理的数据偏移量

                    while(payload_offset < copy_len)
                    {
                        uint32_t chunk = copy_len - payload_offset; // 一般为1024字节

                        // 1. 首先处理头部，直到接收完整个头部为止
                        if(g_ota.hdr_received < OTA_HDR_SIZE)
                        {
                            uint32_t hdr_left = OTA_HDR_SIZE - g_ota.hdr_received;
                            uint32_t hdr_chunk = (chunk < hdr_left) ? chunk : hdr_left;
                            memcpy(g_ota.hdr_buf + g_ota.hdr_received, data + 3 + payload_offset, hdr_chunk);
                            g_ota.hdr_received += hdr_chunk;
                            payload_offset += hdr_chunk;

                            if(g_ota.hdr_received == OTA_HDR_SIZE)
                            {
                                OTA_Header_t hdr;
                                if(!Boot_ParseOtaHeader(g_ota.hdr_buf, OTA_HDR_SIZE, &hdr))
                                {
                                    LOG_E("Invalid OTA header");
                                    OTA_state = UART_CONSOLE_IDLE;
                                    break;
                                }
                                if((hdr.header_size + OTA_META_LEN + hdr.fw_size + hdr.sig_len) != OTA_Info.FileSize)
                                {
                                    LOG_E("OTA size mismatch");
                                    OTA_state = UART_CONSOLE_IDLE;
                                    break;
                                }
                                if(hdr.sig_len > OTA_SIG_MAX || hdr.fw_size == 0)
                                {
                                    LOG_E("OTA header fields invalid");
                                    OTA_state = UART_CONSOLE_IDLE;
                                    break;
                                }
                                if(hdr.fw_size > MCU_FLASH_SLOT_SIZE)
                                {
                                    LOG_E("Firmware too large for slot: %lu", hdr.fw_size);
                                    OTA_state = UART_CONSOLE_IDLE;
                                    break;
                                }

                                g_ota.firmware_size = hdr.fw_size;
                                g_ota.sig_len = hdr.sig_len;
                                OTA_Info.OTA_type = (uint8_t)hdr.pkg_type;

                                W25Q64_EraseSector(OTA_HDR_ADDR / W25Q64_SECTOR_SIZE);
                                W25Q64_WriteBytes(OTA_HDR_ADDR, g_ota.hdr_buf, OTA_HDR_SIZE);

                                g_ota.sha_started = false;
                            }
                            continue;
                        }

                        // 2. 头部接收完成后，处理元数据
                        if(g_ota.meta_received < OTA_META_LEN)
                        {
                            uint32_t meta_left = OTA_META_LEN - g_ota.meta_received;
                            uint32_t meta_chunk = (chunk < meta_left) ? chunk : meta_left;
                            memcpy(g_ota.meta_buf + g_ota.meta_received, data + 3 + payload_offset, meta_chunk);
                            g_ota.meta_received += meta_chunk;
                            payload_offset += meta_chunk;

                            if(g_ota.meta_received == OTA_META_LEN)
                            {
                                // 元数据必须和当前payload保持一致，否则后续解密会使用陈旧key/iv导致写入内容错误。
                                W25Q64_EraseSector(OTA_META_ADDR / W25Q64_SECTOR_SIZE);
                                W25Q64_WriteBytes(OTA_META_ADDR, g_ota.meta_buf, OTA_META_LEN);
                            }
                            continue;
                        }

                        // 3. 处理固件数据，直到接收完整个固件为止
                        uint32_t stream_pos = g_ota.payload_received;
                        if(stream_pos < g_ota.firmware_size)
                        {
                            uint32_t fw_left = g_ota.firmware_size - stream_pos;    // 固件剩余字节数
                            uint32_t fw_chunk = (chunk < fw_left) ? chunk : fw_left;
                            uint32_t fw_written = fw_chunk;

                            // 如果已经开始但还没有接收完整个固件，就继续更新 SHA-256
                            // 将数据写入缓存，满 1024 字节就写入 Flash
                            while(fw_chunk > 0)
                            {
                                uint32_t space = UPDATA_BUFF - Local_UpDate.Ymodem_BytesInBuffer;
                                uint32_t n = (fw_chunk < space) ? fw_chunk : space;
                                memcpy(&Local_UpDate.UpAppBuffer[Local_UpDate.Ymodem_BytesInBuffer],
                                    data + 3 + payload_offset, n);
                                Local_UpDate.Ymodem_BytesInBuffer += n;
                                payload_offset += n;
                                fw_chunk -= n;

                                if(Local_UpDate.Ymodem_BytesInBuffer >= UPDATA_BUFF)
                                {
                                    Ymodem_WriteBlock(Local_UpDate.Ymodem_WriteBlockIndex,
                                        Local_UpDate.UpAppBuffer, UPDATA_BUFF);
                                    Local_UpDate.Ymodem_WriteBlockIndex ++;
                                    Local_UpDate.Ymodem_BytesInBuffer = 0;
                                }
                            }

                            g_ota.payload_received += fw_written;
                        }
                        else    // 4. 处理签名数据，直到接收完整个签名为止
                        {
                            uint32_t sig_offset = stream_pos - g_ota.firmware_size;
                            if(sig_offset >= g_ota.sig_len)
                            {
                                payload_offset += chunk;
                                g_ota.payload_received += chunk;
                            }
                            else    // 接收签名数据，存储到 g_sig_buf 中
                            {
                                uint32_t sig_left = g_ota.sig_len - sig_offset;
                                uint32_t sig_chunk = (chunk < sig_left) ? chunk : sig_left;
                                memcpy(&g_ota.sig_buf[sig_offset], data + 3 + payload_offset, sig_chunk);
                                if(g_ota.sig_received < sig_offset + sig_chunk)
                                    g_ota.sig_received = sig_offset + sig_chunk;
                                W25Q64_WriteBytes(OTA_SIG_ADDR + sig_offset, data + 3 + payload_offset, sig_chunk);
                                payload_offset += sig_chunk;
                                g_ota.payload_received += sig_chunk;
                            }
                        }
                    }

                    Local_UpDate.Ymodem_TotalReceived += copy_len;
                }

                Local_UpDate.Ymodem_ExpectBlock ++;
                Uart_Printf("\x06"); // ACK
            }
            else if(block_num == (uint8_t)(Local_UpDate.Ymodem_ExpectBlock - 1))    // 收到重复的数据块，可能是上一个 ACK 丢失了
            {
                Uart_Printf("\x06"); // ACK duplicate
            }
            else
            {
                Uart_Printf("\x15"); // NAK
            }
            break;
        }
        
        // 设置程序的版本号
        case SET_VERSION:
            if(size == 10)
            {
                // 只是判断版本号格式是否正确，不用提取出参数
                if (sscanf((char *)data, "version%d.%d", 
	                &temp, &temp) == 2)
                {
                    memset(OTA_Info.OTA_version, 0, 32);
                    memcpy(OTA_Info.OTA_version, data, 10); // version1.0
                    W25Q64_WriteOTAInfo();
                    LOG_I("Set version number OK!");
                    OTA_state = UART_CONSOLE_IDLE;
                }
            }
            break;

        // 从 W25Q24 读取数据更新 A 区应用程序
        case UPDATA_FULL_SET: 
			W25Q64_ReadOTAInfo();
            {
                OTA_Header_t hdr;
                if(!Boot_VerifySignatureFromW25Q64(&hdr))
                {
                    LOG_E("Verify package in W25Q64 failed");
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }
                if(hdr.pkg_type != OTA_PKG_TYPE_FULL)
                {
                    LOG_E("Package type mismatch, expect full");
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }

                OTA_Info.FileSize = hdr.fw_size;
                OTA_Info.OTA_type = (uint8_t)hdr.pkg_type;
            }
            Uart_Printf("size: %lu\r\n", OTA_Info.FileSize);

            {
                uint8_t target_slot = Boot_GetInactiveSlot();
                uint32_t target_addr = Boot_GetSlotStartAddr(target_slot);
                MCU_EraseFlash(Boot_GetSlotStartSector(target_slot), Boot_GetSlotSectorCount(target_slot));

                if(OTA_Info.FileSize > MCU_FLASH_SLOT_SIZE)
                {
                    LOG_E("Firmware size exceeds slot: %lu", OTA_Info.FileSize);
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }

                if(!Boot_DecryptW25Q64ToMcu(OTA_STAGING_ADDR, OTA_Info.FileSize, target_addr))
                {
                    LOG_E("Decrypt and write failed");
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }

                OTA_Info.OTA_area = target_slot;
            }

            // 清除 OTA 标志
            OTA_Info.OTA_Flag = 0;
            OTA_Info.OTA_status = UPDATE; // 刚接收但未验证
            W25Q64_WriteOTAInfo();
            LOG_I("Program slot %c from staging OK!", (OTA_Info.OTA_area == MCU_FLASH_APP_A_SLOT) ? 'A' : 'B');

            OTA_state = UART_CONSOLE_IDLE;
            break;

        case UPDATA_DELTA_SET:
        {
            uint8_t active_slot = Boot_GetActiveSlot();
            uint8_t target_slot = Boot_GetInactiveSlot();
            uint32_t base_addr = Boot_GetSlotStartAddr(active_slot);
            uint32_t target_addr = Boot_GetSlotStartAddr(target_slot);
            OTA_Header_t hdr;

            W25Q64_ReadOTAInfo();
            if(!Boot_VerifySignatureFromW25Q64(&hdr))
            {
                LOG_E("Verify package in W25Q64 failed");
                OTA_state = UART_CONSOLE_IDLE;
                break;
            }
            if(hdr.pkg_type != OTA_PKG_TYPE_DELTA)
            {
                LOG_E("Package type mismatch, expect delta");
                OTA_state = UART_CONSOLE_IDLE;
                break;
            }

            OTA_Info.FileSize = hdr.fw_size;
            OTA_Info.OTA_type = (uint8_t)hdr.pkg_type;
            Uart_Printf("delta size: %lu\r\n", OTA_Info.FileSize);
            // Uart_Printf("delta route active=%u target=%u ota_area=%u base=0x%08lX target=0x%08lX\r\n",
            //     active_slot,
            //     target_slot,
            //     OTA_Info.OTA_area,
            //     (unsigned long)base_addr,
            //     (unsigned long)target_addr);

            if(OTA_Info.FileSize == 0U || OTA_Info.FileSize > OTA_STAGING_SIZE)
            {
                LOG_E("Delta size invalid");
                OTA_state = UART_CONSOLE_IDLE;
                break;
            }

            MCU_EraseFlash(Boot_GetSlotStartSector(target_slot), Boot_GetSlotSectorCount(target_slot));
            // 密文差分包全程流式处理：staging密文 -> 解密 -> (tinyuz) -> 差分还原 -> 写入目标槽
            if(!Boot_ApplyDeltaFromW25Q64(OTA_STAGING_ADDR, OTA_Info.FileSize, base_addr, target_addr))
            {
                LOG_E("Delta apply failed");
                OTA_state = UART_CONSOLE_IDLE;
                break;
            }

            OTA_Info.OTA_area = target_slot;
            OTA_Info.OTA_Flag = 0;
            OTA_Info.OTA_status = UPDATE; // 刚接收但未验证

            W25Q64_WriteOTAInfo();
            LOG_I("Delta program to slot %c OK!", (target_slot == MCU_FLASH_APP_A_SLOT) ? 'A' : 'B');

            OTA_state = UART_CONSOLE_IDLE;
            break;
        }

        default:
            OTA_state = UART_CONSOLE_IDLE;
            break;
    }
}

// 设置 A 区 MSP 指针
 __asm void MSP_setSP(uint32_t addr)
 {
     MSR MSP, r0 // ARM 函数调用规范中，第一个参数存在 r0 寄存器
     BX r14
 }

static void LOAD_A(uint32_t addr)
{
    // SP 指针位于 RAM 内存中，地址为 0x2000_0000~0x2001_C000，112KB大小
    if((*(uint32_t *)addr >= SRAM1_BASE) && (*(uint32_t *)addr <= SRAM2_BASE))
    {
        // 中断向量表的初始SP
        MSP_setSP(*(uint32_t *)addr);
//        __set_MSP(*(uint32_t *)addr);
        Load_A = (load_a)*(uint32_t *)(addr + 4); // 中断向量表的reset中断函数
        BootLoader_Clear();
        Load_A(); // reset中断函数
    } else LOG_E("Brance failed!");
}

// 清空外设
static void BootLoader_Clear(void)
{
    // HAL_I2C_MspDeInit(&hi2c1);
    HAL_UART_DMAStop(&huart3); // 必须要先停止 DMA，要不然会和 A 区冲突
    HAL_UART_MspDeInit(&huart3);
    HAL_SPI_MspDeInit(&hspi3);
    HAL_GPIO_DeInit(SPI_CS_GPIO_Port, SPI_CS_Pin);
}
