#include "main.h"
#include "Boot.h"
#include <stdlib.h>
#include <string.h>

load_a Load_A;
uint8_t data[RX_DATA_CELLING];
bool Where_to_store;
static bool BootLoader_Console(uint16_t timeout);
static void BootLoader_Menu(void);
static void BootLoader_Clear(void);
static void LOAD_A(uint32_t addr);
static uint16_t Ymodem_CRC16(uint8_t *pdata, uint16_t length);
static bool Ymodem_CheckPacket(const uint8_t *packet, uint16_t size, uint16_t *data_len, uint8_t *block_num);
static bool Ymodem_ParseHeader(const uint8_t *data, uint16_t data_len, uint32_t *file_size, bool *is_end);
static void Ymodem_WriteBlock(uint32_t block_index, const uint8_t *buf, uint32_t size);
static void Ymodem_Finalize(uint32_t start_time);

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
            Where_to_store = true;
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
            W25Q64_EraseBlock(0);
            OTA_state = IAP_YMODEM_START;
            UpData_A.Ymodem_Timer = 0;
            UpData_A.Ymodem_CRC = 0;
            UpData_A.Ymodem_TotalReceived = 0;
            UpData_A.Ymodem_WriteBlockIndex = 0;
            UpData_A.Ymodem_BytesInBuffer = 0;
            UpData_A.Ymodem_ExpectBlock = 0;
            UpData_A.Ymodem_HeaderReceived = 0;
            Where_to_store = false;
            OTA_Info.FileSize = 0; // 需要一边下载，一边记录程序的大小
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
    static uint32_t start_time = 0;

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

                    OTA_Info.FileSize = file_size;
                    UpData_A.Ymodem_HeaderReceived = 1;
                    UpData_A.Ymodem_ExpectBlock = 1;
                    start_time = HAL_GetTick();

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
                uint32_t remaining = data_len;  // 计算剩余需要接收的字节数
                if(OTA_Info.FileSize > 0)
                {
                    if(UpData_A.Ymodem_TotalReceived >= OTA_Info.FileSize)
                        remaining = 0;
                    else if(UpData_A.Ymodem_TotalReceived + data_len > OTA_Info.FileSize)
                        remaining = OTA_Info.FileSize - UpData_A.Ymodem_TotalReceived;
                }

                if(remaining > 0)
                {
                    uint16_t copy_len = (uint16_t)remaining;
                    memcpy(&UpData_A.UpAppBuffer[UpData_A.Ymodem_BytesInBuffer], data + 3, copy_len);
                    UpData_A.Ymodem_BytesInBuffer += copy_len;
                    UpData_A.Ymodem_TotalReceived += copy_len;

                    if(UpData_A.Ymodem_BytesInBuffer >= UPDATA_BUFF)
                    {
                        Ymodem_WriteBlock(UpData_A.Ymodem_WriteBlockIndex,
                            UpData_A.UpAppBuffer, UPDATA_BUFF);
                        UpData_A.Ymodem_WriteBlockIndex ++;
                        UpData_A.Ymodem_BytesInBuffer = 0;
                    }
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
                    Ymodem_Finalize(start_time);
                    OTA_state = UART_CONSOLE_IDLE;
                    break;
                }
            }

            if(UpData_A.Ymodem_Timer >= 200)    // 每 2 秒发送一次申请，直到收到最后的空头包
            {
                Ymodem_Finalize(start_time);
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
                W25Q64_ReadData((i * UPDATA_BUFF)/W25Q64_PAGE_SIZE, 
                UpData_A.UpAppBuffer, UPDATA_BUFF);

                // 1024 字节为单位向 MCU 的 Flash 写入应用程序
                MCU_WriteFlash(MCU_FLASH_A_START_ADDRESS + i * UPDATA_BUFF, 
                    (uint32_t *)UpData_A.UpAppBuffer, UPDATA_BUFF/4);
            }
            if(OTA_Info.FileSize % UPDATA_BUFF != 0)
            {
                // 读取剩余应用程序
                W25Q64_ReadData((i * UPDATA_BUFF)/W25Q64_PAGE_SIZE, 
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

static uint16_t Ymodem_CRC16(uint8_t *pdata, uint16_t length)
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
static bool Ymodem_CheckPacket(const uint8_t *packet, uint16_t size, uint16_t *data_len, uint8_t *block_num)
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
static bool Ymodem_ParseHeader(const uint8_t *data, uint16_t data_len, uint32_t *file_size, bool *is_end)
{
    const char *name = (const char *)data;
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

    const char *size_str = name + name_len + 1;
    if(size_str >= (const char *)(data + data_len))
        return false;

    *file_size = (uint32_t)strtoul(size_str, NULL, 10);
    *is_end = false;        // 头包不可能是结束包
    return true;
}

/**
 * @brief  Ymodem 数据块写入函数，将接收到的数据块写入指定存储介质
 * @param  block_index: 数据块的索引，从 0 开始
 * @param  buf: 指向数据块内容的指针
 * @param  size: 数据块的大小，单位字节
 * @retval None
 */
static void Ymodem_WriteBlock(uint32_t block_index, const uint8_t *buf, uint32_t size)
{
    if(size == 0)
        return;

    if(Where_to_store)      // 写入 MCU 的 Flash
    {
        MCU_WriteFlash(MCU_FLASH_A_START_ADDRESS + block_index * UPDATA_BUFF,
            (uint32_t *)buf, size / 4);
    }
    else    // 写入 W25Q64
    {
        uint32_t page_base = block_index * (UPDATA_BUFF / W25Q64_PAGE_SIZE);
        uint32_t offset = 0;    // 因为 W25Q64 的页大小是 256 字节，而我们每次写入 1024 字节，所以需要分 4 页来写入
        while(offset < size)
        {
            uint32_t chunk = size - offset;
            if(chunk > W25Q64_PAGE_SIZE)
                chunk = W25Q64_PAGE_SIZE;
            W25Q64_PageProgram((uint16_t)(page_base + (offset / W25Q64_PAGE_SIZE)),
                (uint8_t *)(buf + offset), chunk);
            offset += chunk;
        }
    }
}

/**
 * @brief  Ymodem 传输完成处理函数，进行必要的收尾工作，如写入剩余数据、记录文件大小、清除 OTA 标志等
 * @param  start_time: 传输开始的时间戳，用于计算传输耗时
 * @retval None
 */
static void Ymodem_Finalize(uint32_t start_time)
{
    uint32_t end_time = HAL_GetTick();
    
    if(UpData_A.Ymodem_BytesInBuffer > 0)
    {
        Ymodem_WriteBlock(UpData_A.Ymodem_WriteBlockIndex,
            UpData_A.UpAppBuffer, UpData_A.Ymodem_BytesInBuffer);
    }
    if(OTA_Info.FileSize == 0)
        OTA_Info.FileSize = UpData_A.Ymodem_TotalReceived;

    AT24C64_WriteOTAInfo();
    LOG_I("Download A program OK!");
    LOG_I("Time: %dms", end_time - start_time);
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
