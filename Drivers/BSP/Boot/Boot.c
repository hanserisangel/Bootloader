#include "main.h"
#include "Boot.h"
#include "Ymodem.h"
#include <stdlib.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls.h"

load_a Load_A;
static uint8_t data[RX_DATA_CELLING];
static bool Where_to_store;

// 初级阶段的 Bootloader 设计
static bool BootLoader_Console(uint16_t timeout);
static void BootLoader_Menu(void);
static void BootLoader_Clear(void);
static void LOAD_A(uint32_t addr);

// 进阶阶段的 Bootloader 设计
static void Boot_ResetVerifyState(void);
static void Boot_ReadW25Q64Bytes(uint32_t addr, uint8_t *out, uint32_t len);
static bool Boot_ParseOtaHeader(const uint8_t *buf, uint32_t len, OTA_Header_t *out);

static mbedtls_sha256_context g_sha_ctx;        // 全局 SHA-256 上下文
static bool g_sha_started = false;              // 是否已经开始计算 SHA-256

static uint32_t g_firmware_size = 0;            // 固件数据大小
static uint32_t g_sig_received = 0;             // 已经接收到的签名字节数
static uint32_t g_sig_len = 0;                  // 头部解析出来的签名长度
static uint32_t g_payload_received = 0;         // 已接收的固件+签名字节数(不含头部)
static uint32_t g_hdr_received = 0;             // 已接收的头部字节数
static uint8_t g_hdr_buf[OTA_HDR_SIZE];         // OTA 头部缓存

static uint8_t g_sig_buf[OTA_SIG_MAX];          // 接收签名的缓冲区

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
            AT24C64_ReadOTAInfo();
            Uart_Printf("version number: %s\r\n", OTA_Info.OTA_version);
            break;
        case '5': // 串口 IAP 下载 A 区程序到 W25Q64
            LOG_I("Download Starting!");
            W25Q64_EraseBlock(OTA_Firmware_A_ADDR / W25Q64_BLOCK_SIZE); // 擦除 W25Q64 中存储固件的块
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
                    if(OTA_Info.FileSize <= OTA_HDR_SIZE)
                    {
                        LOG_E("File size too small for header");
                        Uart_Printf("\x15"); // NAK
                        OTA_state = UART_CONSOLE_IDLE;
                        break;
                    }
                    g_sig_received = 0;
                    g_sig_len = 0;
                    g_firmware_size = 0;
                    g_payload_received = 0;
                    g_hdr_received = 0;
                    g_sha_started = false;
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
                        if(g_hdr_received < OTA_HDR_SIZE)
                        {
                            uint32_t hdr_left = OTA_HDR_SIZE - g_hdr_received;
                            uint32_t hdr_chunk = (chunk < hdr_left) ? chunk : hdr_left;
                            memcpy(g_hdr_buf + g_hdr_received, data + 3 + payload_offset, hdr_chunk);
                            g_hdr_received += hdr_chunk;
                            payload_offset += hdr_chunk;

                            if(g_hdr_received == OTA_HDR_SIZE)
                            {
                                OTA_Header_t hdr;
                                if(!Boot_ParseOtaHeader(g_hdr_buf, OTA_HDR_SIZE, &hdr))
                                {
                                    LOG_E("Invalid OTA header");
                                    OTA_state = UART_CONSOLE_IDLE;
                                    break;
                                }
                                if((hdr.header_size + hdr.fw_size + hdr.sig_len) != OTA_Info.FileSize)
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

                                g_firmware_size = hdr.fw_size;
                                g_sig_len = hdr.sig_len;
                                mbedtls_sha256_init(&g_sha_ctx);
                                mbedtls_sha256_starts(&g_sha_ctx, 0);
                                g_sha_started = true;
                            }
                            continue;
                        }

                        // 2. 头部接收完成后，处理固件数据，直到接收完整个固件为止
                        uint32_t stream_pos = g_payload_received;
                        if(stream_pos < g_firmware_size)
                        {
                            uint32_t fw_left = g_firmware_size - stream_pos;    // 固件剩余字节数
                            uint32_t fw_chunk = (chunk < fw_left) ? chunk : fw_left;
                            uint32_t fw_written = fw_chunk; // 一般1024字节

                            // 如果已经开始但还没有接收完整个固件，就继续更新 SHA-256
                            if(g_sha_started)
                                mbedtls_sha256_update(&g_sha_ctx, data + 3 + payload_offset, fw_chunk);

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

                            g_payload_received += fw_written;
                        }
                        else    // 3. 固件数据接收完成后，处理签名数据，直到接收完整个签名为止
                        {
                            uint32_t sig_offset = stream_pos - g_firmware_size;
                            if(sig_offset >= g_sig_len)
                            {
                                payload_offset += chunk;
                                g_payload_received += chunk;
                            }
                            else    // 接收签名数据，存储到 g_sig_buf 中
                            {
                                uint32_t sig_left = g_sig_len - sig_offset;
                                uint32_t sig_chunk = (chunk < sig_left) ? chunk : sig_left;
                                memcpy(&g_sig_buf[sig_offset], data + 3 + payload_offset, sig_chunk);
                                if(g_sig_received < sig_offset + sig_chunk)
                                    g_sig_received = sig_offset + sig_chunk;
                                payload_offset += sig_chunk;
                                g_payload_received += sig_chunk;
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
                    Ymodem_Finalize(Where_to_store, g_firmware_size);
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }
            }

            if(UpData_A.Ymodem_Timer >= 200)    // 每 2 秒发送一次申请，直到收到最后的空头包
            {
                Ymodem_Finalize(Where_to_store, g_firmware_size);
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
                    AT24C64_WriteOTAInfo();
                    LOG_I("Set version number OK!");
                    OTA_state = UART_CONSOLE_IDLE;
                }
            }
            break;

        // 从 W25Q24 读取数据更新 A 区应用程序
        case UPDATA_A_SET: 
			AT24C64_ReadOTAInfo();
            Uart_Printf("size: %d\r\n", OTA_Info.FileSize);
            // if(UpData_A.W25Q24_Block % 4 == 0) // 4 字节已对齐, 因为 flash 是一次写入 4 个字节
            // {
            uint8_t i = 0;
            for(i = 0; i < OTA_Info.FileSize/UPDATA_BUFF; i ++)
            {
                // 1024 字节为单位从 W25Q64 读取应用程序
                Boot_ReadW25Q64Bytes(OTA_Firmware_A_ADDR + i * UPDATA_BUFF, 
                UpData_A.UpAppBuffer, UPDATA_BUFF);

                // 1024 字节为单位向 MCU 的 Flash 写入应用程序
                MCU_WriteFlash(MCU_FLASH_A_START_ADDRESS + i * UPDATA_BUFF, 
                    (uint32_t *)UpData_A.UpAppBuffer, UPDATA_BUFF/4);
            }
            if(OTA_Info.FileSize % UPDATA_BUFF != 0)
            {
                // 读取剩余应用程序
                Boot_ReadW25Q64Bytes(OTA_Firmware_A_ADDR + i * UPDATA_BUFF, 
                UpData_A.UpAppBuffer, OTA_Info.FileSize % UPDATA_BUFF);
                
                // 读取写入应用程序
                MCU_WriteFlash(MCU_FLASH_A_START_ADDRESS + i * UPDATA_BUFF, 
                    (uint32_t *)UpData_A.UpAppBuffer, OTA_Info.FileSize % UPDATA_BUFF/4);
            }

            // 清除 OTA 标志
            OTA_Info.OTA_Flag = 0;
            AT24C64_WriteOTAInfo();
            LOG_I("Download A program from W25Q64 OK!");

            OTA_state = UART_CONSOLE_IDLE;
            break;
        default:
            OTA_state = UART_CONSOLE_IDLE;
            break;
    }
}

/**
 * @brief  Bootloader 验证状态重置函数
 * @note    在开始新的 OTA 传输时调用，重置所有与 OTA 验证相关的状态变量和缓冲区，确保新的传输过程不会受到之前状态的影响
 * @param  None
 * @retval None
 */
static void Boot_ResetVerifyState(void)
{
    if(g_sha_started)
        mbedtls_sha256_free(&g_sha_ctx);
    g_sha_started = false;
    g_firmware_size = 0;
    g_sig_received = 0;
    g_sig_len = 0;
    g_payload_received = 0;
    g_hdr_received = 0;
    mbedtls_platform_zeroize(g_hdr_buf, sizeof(g_hdr_buf));
    mbedtls_platform_zeroize(g_sig_buf, sizeof(g_sig_buf));
}

/**
 * @brief  Bootloader 从 W25Q64 读取数据的函数，支持跨页读取
 * @param  addr: 读取的起始地址
 * @param  out: 输出缓冲区指针，读取的数据将存储在这里
 * @param  len: 需要读取的字节数
 * @retval None
 */
static void Boot_ReadW25Q64Bytes(uint32_t addr, uint8_t *out, uint32_t len)
{
    while(len > 0)
    {
        uint16_t page = (uint16_t)(addr / W25Q64_PAGE_SIZE);    // 计算当前地址所在的页号
        uint32_t offset = addr % W25Q64_PAGE_SIZE;              // 计算当前地址在页内的偏移
        uint32_t chunk = W25Q64_PAGE_SIZE - offset;             // 计算当前页剩余的字节数
        if(chunk > len)
            chunk = len;

        if(offset == 0 && chunk == W25Q64_PAGE_SIZE)    // 如果当前地址已经页对齐，并且需要读取整页数据，可以直接读取到输出缓冲区，避免中间的复制步骤
        {
            W25Q64_ReadData(page, out, chunk);
        }
        else        // 否则需要先读取到临时缓冲区，再从中复制到输出缓冲区
        {
            uint8_t temp[W25Q64_PAGE_SIZE];
            W25Q64_ReadData(page, temp, W25Q64_PAGE_SIZE);
            memcpy(out, temp + offset, chunk);
        }

        addr += chunk;  // 更新地址指针
        out += chunk;   // 更新输出缓冲区指针
        len -= chunk;   // 更新剩余字节数
    }
}

/**
 * @brief  Bootloader 签名验证函数
 * @note    使用 SHA-256 计算固件数据的摘要，并使用存储在 W25Q64 中的公钥验证签名的合法性
 * @param  None
 * @retval bool: true 验证成功，false 验证失败
 */
bool Boot_VerifySignature(void)
{
    uint8_t digest[32];
    uint8_t pubkey_buf[OTA_PUBKEY_LEN];     // 从 W25Q64 读取公钥
    mbedtls_pk_context pk;          // 公钥上下文
    int ret = 0;

    if(!g_sha_started)
        return false;
    if(g_sig_len == 0 || g_sig_received < g_sig_len)
        return false;

    mbedtls_sha256_finish(&g_sha_ctx, digest);  // 计算 SHA-256 摘要
    mbedtls_sha256_free(&g_sha_ctx);            // 释放 SHA-256 上下文
    g_sha_started = false;

    Boot_ReadW25Q64Bytes(OTA_PUBKEY_ADDR, pubkey_buf, OTA_PUBKEY_LEN);  // 从 W25Q64 读取公钥数据

    mbedtls_pk_init(&pk);       // 初始化公钥上下文
    ret = mbedtls_pk_parse_public_key(&pk, pubkey_buf, OTA_PUBKEY_LEN); // 解析公钥
    if(ret != 0)
    {
        mbedtls_pk_free(&pk);
        return false;
    }

    // 验证签名，digest 是固件数据的 SHA-256 摘要，g_sig_buf 是接收到的签名数据
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 0, g_sig_buf, g_sig_len);
    mbedtls_pk_free(&pk);

    // 验证完成后清除敏感数据
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return (ret == 0);
}

/**
 * @brief  Bootloader 解析 OTA 头部函数
 * @note    从接收到的数据中解析出 OTA 头部信息，包括魔数、头部大小、固件大小和签名长度，并进行基本的合法性验证
 * @param  buf: 指向包含 OTA 头部数据的缓冲区的指针
 * @param  len: 缓冲区中数据的长度
 * @param  out: 输出参数，解析成功后将 OTA 头部信息存储在这里
 * @retval bool: true 验证成功，false 验证失败
 */
static bool Boot_ParseOtaHeader(const uint8_t *buf, uint32_t len, OTA_Header_t *out)
{
    if(len < OTA_HDR_SIZE || out == NULL)
        return false;

    // stm32 是小端序，Ymodem 传输过来的数据也是小端序
    // 将4个字节的字段从字节数组中解析出来，组合成一个 32 位的整数
    uint32_t magic = (uint32_t)buf[0] |
        ((uint32_t)buf[1] << 8) |
        ((uint32_t)buf[2] << 16) |
        ((uint32_t)buf[3] << 24);
    uint32_t header_size = (uint32_t)buf[4] |
        ((uint32_t)buf[5] << 8) |
        ((uint32_t)buf[6] << 16) |
        ((uint32_t)buf[7] << 24);
    uint32_t fw_size = (uint32_t)buf[8] |
        ((uint32_t)buf[9] << 8) |
        ((uint32_t)buf[10] << 16) |
        ((uint32_t)buf[11] << 24);
    uint32_t sig_len = (uint32_t)buf[12] |
        ((uint32_t)buf[13] << 8) |
        ((uint32_t)buf[14] << 16) |
        ((uint32_t)buf[15] << 24);

    if(magic != OTA_HDR_MAGIC || header_size != OTA_HDR_SIZE)
        return false;

    out->magic = magic;
    out->header_size = header_size;
    out->fw_size = fw_size;
    out->sig_len = sig_len;
    return true;
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
    HAL_I2C_MspDeInit(&hi2c1);
    HAL_UART_DMAStop(&huart3); // 必须要先停止 DMA，要不然会和 A 区冲突
    HAL_UART_MspDeInit(&huart3);
    HAL_SPI_MspDeInit(&hspi3);
    HAL_GPIO_DeInit(SPI_CS_GPIO_Port, SPI_CS_Pin);
}
