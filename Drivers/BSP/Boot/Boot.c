#include "main.h"
#include "Boot.h"
#include "Ymodem.h"
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "BootOta.h"
#include "BootCrypto.h"

load_a Load_A;
static uint8_t data[RX_DATA_CELLING];
static bool Where_to_store;

// 初级阶段的 Bootloader 设计
static bool BootLoader_Console(uint16_t timeout);
static void BootLoader_Menu(void);
static void BootLoader_Clear(void);
static void LOAD_A(uint32_t addr);

// 进阶阶段的 Bootloader 设计
// OTA verify state is kept in BootOta.c (g_ota)

/**
 * @brief  Bootloader 主函数，负责引导流程控制
 * @retval None
 */
void BootLoader_Brance(void)
{
    if(BootLoader_Console(20))
    {
        // 进入命令行
        LOG_I("Enter console success!");
        BootLoader_Menu();
        OTA_state = UART_CONSOLE_IDLE;
    }
    else // 不进入命令行，直接进行 OTA 跳转功能
    {
        if(OTA_Info.OTA_Flag == OTA_FLAG)
        {
            // 更新A区应用程序
            LOG_I("OTA update!");
			MCU_EraseFlash(MCU_FLASH_A_START_PAGE, MCU_FLASH_A_PAGE_NUM);
            OTA_state = UPDATA_A_SET; // 设置状态为更新 A 区
        }
        else
        {
            // 直接跳转到A区执行应用程序
            LOG_I("OTA brance!");
            LOAD_A(MCU_FLASH_A_START_ADDRESS);
        }
    }
}

/**
 * @brief  菜单显示函数，列出可用的串口命令选项
 * @retval None
 */
static void BootLoader_Menu(void)
{
    Uart_Printf("[1] Erase A area\r\n");
    Uart_Printf("[2] Uart IAP download A program\r\n");
    Uart_Printf("[3] Set version number\r\n");
    Uart_Printf("[4] Query version number\r\n");
    Uart_Printf("[5] Download A program into W25Q64\r\n");
    Uart_Printf("[6] Download A program from W25Q64\r\n");
    Uart_Printf("[7] Reset\r\n");
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
        case '1': // 擦除 A 区
            MCU_EraseFlash(MCU_FLASH_A_START_PAGE, MCU_FLASH_A_PAGE_NUM);
            LOG_I("Erase A area OK!");
            break;
        case '2': // 串口 IAP 下载 A 区程序到 MCU_Flash
            LOG_I("Uart IAP download A program Starting!");
            MCU_EraseFlash(MCU_FLASH_A_START_PAGE, MCU_FLASH_A_PAGE_NUM); // 擦除 MCU 的 A 区的 Flash
            OTA_state = IAP_YMODEM_START; // 向 pc 发送申请
            UpData_A.Ymodem_Timer = 0;
            UpData_A.Ymodem_CRC = 0;
            UpData_A.Ymodem_TotalReceived = 0;
            UpData_A.Ymodem_WriteBlockIndex = 0;
            UpData_A.Ymodem_BytesInBuffer = 0;
            UpData_A.Ymodem_ExpectBlock = 0;
            UpData_A.Ymodem_HeaderReceived = 0;
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

            // 擦除 W25Q64 中存储固件的512KB，准备接收新的固件数据
            LOG_I("Erase W25Q64 block...");
            uint32_t block_addr = (OTA_Info.OTA_area == 0) ? OTA_Firmware_A_ADDR : OTA_Firmware_B_ADDR; // 计算需要擦除的块地址
            for(uint32_t addr = block_addr; addr < block_addr + 512 * 1024; addr += W25Q64_BLOCK_SIZE)
            {
                W25Q64_EraseBlock(addr / W25Q64_BLOCK_SIZE); // 擦除 W25Q64 中存储固件的块
            }
            LOG_I("Erase W25Q64 block OK!");
            
            OTA_state = IAP_YMODEM_START;
            UpData_A.Ymodem_Timer = 0;
            UpData_A.Ymodem_CRC = 0;
            UpData_A.Ymodem_TotalReceived = 0;
            UpData_A.Ymodem_WriteBlockIndex = 0;
            UpData_A.Ymodem_BytesInBuffer = 0;
            UpData_A.Ymodem_ExpectBlock = 0;
            UpData_A.Ymodem_HeaderReceived = 0;
            Boot_ResetVerifyState();
            Where_to_store = false;              // 设置存储位置为 W25Q64
            OTA_Info.FileSize = 0;
            break;
        case '6': // 从 W25Q64 下载程序到 MCU 的 Flash
            LOG_I("Download Starting!");
            MCU_EraseFlash(MCU_FLASH_A_START_PAGE, MCU_FLASH_A_PAGE_NUM); // 擦除 MCU 的 A 区的 Flash
            OTA_state = UPDATA_A_SET;
            break;
        case '7': // 重启
            LOG_I("Reset OK!");
            HAL_Delay(100);
            HAL_NVIC_SystemReset();
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
                    UpData_A.Ymodem_HeaderReceived = 1;
                    UpData_A.Ymodem_ExpectBlock = 1;

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
                if(UpData_A.Ymodem_Timer >= 100)    // 每 1 秒发送一次申请，直到收到头包
                {
                    Uart_Printf("%c", CRC_REQ);
                    UpData_A.Ymodem_Timer = 0;
                }
                UpData_A.Ymodem_Timer ++;
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
                Uart_Printf("\x06"); // ACK
                Uart_Printf("%c", CRC_REQ); // Request final empty header
                UpData_A.Ymodem_Timer = 0;
                OTA_state = IAP_YMODEM_END;
                break;
            }

            if(!Ymodem_CheckPacket(data, size, &data_len, &block_num))  // 数据包校验失败
                break;

            if(block_num == UpData_A.Ymodem_ExpectBlock)    // 收到期望的数据块
            {
                uint32_t remaining = data_len;  // 计算剩余需要接收的字节数，data_len=1024

                // 如果已经知道了文件总大小，就根据文件总大小和已经接收的字节数来计算当前数据块中实际需要处理的字节数，避免处理多余的数据
                if(OTA_Info.FileSize > 0)
                {
                    if(UpData_A.Ymodem_TotalReceived >= OTA_Info.FileSize)
                        remaining = 0;
                    else if(UpData_A.Ymodem_TotalReceived + data_len > OTA_Info.FileSize)
                        remaining = OTA_Info.FileSize - UpData_A.Ymodem_TotalReceived;
                }

                if(remaining > 0)
                {
                    uint32_t copy_len = remaining;      // 实际需要处理的数据长度，正常是 1024
                    uint32_t payload_offset = 0;        // 已经处理的数据偏移量

                    while(payload_offset < copy_len)
                    {
                        uint32_t chunk = copy_len - payload_offset;

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

                                g_ota.firmware_size = hdr.fw_size;
                                g_ota.sig_len = hdr.sig_len;
                                mbedtls_sha256_init(&g_ota.sha_ctx);
                                mbedtls_sha256_starts(&g_ota.sha_ctx, 0);
                                mbedtls_sha256_update(&g_ota.sha_ctx, g_ota.hdr_buf, OTA_HDR_SIZE);
                                g_ota.sha_started = true;
                            }
                            continue;
                        }

                        // 如果头部已经接收完成但元数据还没有接收完成，就继续处理元数据，直到接收完整个元数据为止
                        if(g_ota.meta_received < OTA_META_LEN)
                        {
                            uint32_t meta_left = OTA_META_LEN - g_ota.meta_received;
                            uint32_t meta_chunk = (chunk < meta_left) ? chunk : meta_left;
                            memcpy(g_ota.meta_buf + g_ota.meta_received, data + 3 + payload_offset, meta_chunk);
                            if(g_ota.sha_started)
                                mbedtls_sha256_update(&g_ota.sha_ctx, data + 3 + payload_offset, meta_chunk);
                            g_ota.meta_received += meta_chunk;
                            payload_offset += meta_chunk;

                            if(g_ota.meta_received == OTA_META_LEN)
                            {
                                if(!Where_to_store)
                                    W25Q64_WriteBytes(OTA_META_ADDR, g_ota.meta_buf, OTA_META_LEN);
                            }
                            continue;
                        }

                        // 2. 头部接收完成后，处理固件数据，直到接收完整个固件为止
                        uint32_t stream_pos = g_ota.payload_received;
                        if(stream_pos < g_ota.firmware_size)
                        {
                            uint32_t fw_left = g_ota.firmware_size - stream_pos;    // 固件剩余字节数
                            uint32_t fw_chunk = (chunk < fw_left) ? chunk : fw_left;
                            uint32_t fw_written = fw_chunk; // 一般1024字节

                            // 如果已经开始但还没有接收完整个固件，就继续更新 SHA-256
                            if(g_ota.sha_started)
                                mbedtls_sha256_update(&g_ota.sha_ctx, data + 3 + payload_offset, fw_chunk);

                            // 将数据写入缓存，满 1024 字节就写入 Flash
                            while(fw_chunk > 0)
                            {
                                uint32_t space = UPDATA_BUFF - UpData_A.Ymodem_BytesInBuffer;
                                uint32_t n = (fw_chunk < space) ? fw_chunk : space;
                                memcpy(&UpData_A.UpAppBuffer[UpData_A.Ymodem_BytesInBuffer],
                                    data + 3 + payload_offset, n);
                                UpData_A.Ymodem_BytesInBuffer += n;
                                payload_offset += n;
                                fw_chunk -= n;

                                if(UpData_A.Ymodem_BytesInBuffer >= UPDATA_BUFF)
                                {
                                    Ymodem_WriteBlock(UpData_A.Ymodem_WriteBlockIndex,
                                        UpData_A.UpAppBuffer, UPDATA_BUFF, Where_to_store);
                                    UpData_A.Ymodem_WriteBlockIndex ++;
                                    UpData_A.Ymodem_BytesInBuffer = 0;
                                }
                            }

                            g_ota.payload_received += fw_written;
                        }
                        else    // 3. 固件数据接收完成后，处理签名数据，直到接收完整个签名为止
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
                                payload_offset += sig_chunk;
                                g_ota.payload_received += sig_chunk;
                            }
                        }
                    }

                    UpData_A.Ymodem_TotalReceived += copy_len;
                }

                UpData_A.Ymodem_ExpectBlock ++;
                Uart_Printf("\x06"); // ACK
            }
            else if(block_num == (uint8_t)(UpData_A.Ymodem_ExpectBlock - 1))    // 收到重复的数据块，可能是上一个 ACK 丢失了
            {
                Uart_Printf("\x06"); // ACK duplicate
            }
            else
            {
                Uart_Printf("\x15"); // NAK
            }
            break;
        }

        case IAP_YMODEM_END:
        {
            uint16_t data_len = 0;
            uint8_t block_num = 0;
            bool is_end = false;
            uint32_t file_size = 0;

            // 解析最后的空头包，确认传输结束
            if(Ymodem_CheckPacket(data, size, &data_len, &block_num) && (block_num == 0))
            {
                if(Ymodem_ParseHeader(data + 3, data_len, &file_size, &is_end) && is_end)
                {
                    Uart_Printf("\x06"); // ACK
                    Ymodem_Finalize(Where_to_store, g_ota.firmware_size);
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }
            }

            if(UpData_A.Ymodem_Timer >= 200)    // 每 2 秒发送一次申请，直到收到最后的空头包
            {
                Ymodem_Finalize(Where_to_store, g_ota.firmware_size);
                OTA_state = UART_CONSOLE_IDLE;
            }
            else
            {
                UpData_A.Ymodem_Timer ++;
            }
            break;
        }
        
        // 设置程序的版本号
        case SET_VERSION:
            if(size == 28)
            {
                // 只是判断版本号格式是否正确，不用提取出参数
                if (sscanf((char *)data, "version-%d.%d_%4d-%2d-%2d_%2d.%2d", 
	                &temp, &temp, &temp, &temp, &temp, &temp, &temp) == 7)
                {
                    memset(OTA_Info.OTA_version, 0, 32);
                    memcpy(OTA_Info.OTA_version, data, 28);
                    W25Q64_WriteOTAInfo();
                    LOG_I("Set version number OK!");
                    OTA_state = UART_CONSOLE_IDLE;
                }
            }
            break;

        // 从 W25Q24 读取数据更新 A 区应用程序
        case UPDATA_A_SET: 
			W25Q64_ReadOTAInfo();
            Uart_Printf("size: %d\r\n", OTA_Info.FileSize);

            uint32_t block_addr = (OTA_Info.OTA_area == 0) ? OTA_Firmware_A_ADDR : OTA_Firmware_B_ADDR; // 计算固件所在的块地址
            if(!Boot_DecryptW25Q64ToMcu(block_addr, OTA_Info.FileSize, MCU_FLASH_A_START_ADDRESS))
            {
                LOG_E("Decrypt and write failed");
                OTA_state = UART_CONSOLE_IDLE;
                break;
            }

            // 清除 OTA 标志
            OTA_Info.OTA_Flag = 0;
            W25Q64_WriteOTAInfo();
            LOG_I("Download A program from W25Q64 OK!");

            OTA_state = UART_CONSOLE_IDLE;
            break;
        default:
            OTA_state = UART_CONSOLE_IDLE;
            break;
    }
}

// OTA helpers are in BootOta.c
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
    HAL_I2C_MspDeInit(&hi2c1);
    HAL_UART_DMAStop(&huart3); // 必须要先停止 DMA，要不然会和 A 区冲突
    HAL_UART_MspDeInit(&huart3);
    HAL_SPI_MspDeInit(&hspi3);
    HAL_GPIO_DeInit(SPI_CS_GPIO_Port, SPI_CS_Pin);
}
