#include "main.h"
#include "spi.h"
#include "W25Q64.h"

/* W25Q64: 64Mbit = 8MByte, 
    共128个块(Blocks)，每块64KB，
    每块包含16个扇区(Sectors)，每扇区4KB，
    每扇区包含16页(Pages)，每页256字节。
*/

void W25Q64_Init(void)
{
    // 初始化 SPI 接口
    MX_SPI3_Init();
}

/**
 * @brief  等待写操作完成。
 * @retval None
 */
static void W25Q64_WaitForWriteEnd(void)
{
    // 通过 SPI 接口查询状态寄存器，直到写操作完成
    uint8_t status = 0;
    uint8_t cmd = 0x05; // 读取状态寄存器命令
    do
    {
        HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET); // 使能芯片

        // 发送读取状态寄存器命令并读取状态
        HAL_SPI_Transmit(&hspi3, &cmd, 1, HAL_MAX_DELAY);
        HAL_SPI_Receive(&hspi3, &status, 1, HAL_MAX_DELAY);

        HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET); // 禁用芯片
    } while (status & 0x01); // 检查写入进行位
}

/**
 * @brief  写入使能。
 * @retval None
 */
static void W25Q64_WriteEnable(void)
{
    W25Q64_WaitForWriteEnd(); // 确保之前的写操作已完成
    uint8_t cmd = 0x06; // 写使能命令

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET); // 使能芯片

    // 发送写使能命令
    HAL_SPI_Transmit(&hspi3, &cmd, 1, HAL_MAX_DELAY);

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET); // 禁用芯片
}

/**
 * @brief  读取指定页的数据。
 * @param  PageNum: 页号，范围0~32767。
 * @param  pData: 指向存储读取数据的指针。
 * @param  Size: 要读取的数据长度（以字节为单位）。
 * @retval None
 */
void W25Q64_ReadData(uint16_t PageNum, uint8_t* pData, uint32_t Size)
{
    uint8_t cmd[4];
    uint32_t addr = PageNum * W25Q64_PAGE_SIZE; // 计算页地址

    cmd[0] = 0x03; // 读取数据命令
    cmd[1] = (addr >> 16) & 0xFF; // 地址高字节
    cmd[2] = (addr >> 8) & 0xFF;  // 地址中字节
    cmd[3] = addr & 0xFF;         // 地址低字节

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET); // 使能芯片

    HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY); // 发送读取命令和地址
    HAL_SPI_Receive(&hspi3, pData, Size, HAL_MAX_DELAY); // 接收数据

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET); // 禁用芯片
}

/**
 * @brief  写入数据到指定页。
 * @param  PageNum: 页号，范围0~32767。
 * @param  pData: 指向要写入数据的指针。
 * @param  Size: 要写入的数据长度（以字节为单位）。
 * @retval None
 */
void W25Q64_PageProgram(uint16_t PageNum, uint8_t* pData, uint32_t Size)
{
    uint8_t cmd[4];
    uint32_t addr = PageNum * W25Q64_PAGE_SIZE; // 计算页地址

    W25Q64_WriteEnable(); // 使能写操作
    cmd[0] = 0x02; // 页编程命令
    cmd[1] = (addr >> 16) & 0xFF; // 地址高字节
    cmd[2] = (addr >> 8) & 0xFF;  // 地址中字节
    cmd[3] = addr & 0xFF;         // 地址低字节
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET); // 使能芯片

    HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY); // 发送页编程命令和地址
    HAL_SPI_Transmit(&hspi3, pData, Size, HAL_MAX_DELAY); // 发送要写入的数据

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET); // 禁用芯片
    W25Q64_WaitForWriteEnd(); // 等待写入完成
}

/**
 * @brief  擦除指定块。
 * @param  BlockNum: 块号，范围0~127。
 * @retval None
 */
void W25Q64_EraseBlock(uint8_t BlockNum)
{
    uint8_t cmd[4];
    uint32_t addr = BlockNum * 64 * 1024; // 计算块地址

    W25Q64_WriteEnable(); // 使能写操作
    cmd[0] = 0xD8; // 块擦除命令
    cmd[1] = (addr >> 16) & 0xFF; // 地址高字节
    cmd[2] = (addr >> 8) & 0xFF;  // 地址中字节
    cmd[3] = addr & 0xFF;         // 地址低字节
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET); // 使能芯片

    HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY); // 发送块擦除命令和地址

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET); // 禁用芯片
    W25Q64_WaitForWriteEnd(); // 等待擦除完成
}

/**
 * @brief  读取指定地址的数据。
 * @param  Address: 字节地址。
 * @param  pData: 指向存储读取数据的指针。
 * @param  Size: 要读取的数据长度（以字节为单位）。
 * @retval None
 */
void W25Q64_ReadBytes(uint32_t Address, uint8_t* pData, uint32_t Size)
{
    uint8_t cmd[4];

    cmd[0] = 0x03; // read data
    cmd[1] = (Address >> 16) & 0xFF;
    cmd[2] = (Address >> 8) & 0xFF;
    cmd[3] = Address & 0xFF;

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi3, pData, Size, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief  写入指定地址的数据（自动处理跨页）。
 * @param  Address: 字节地址。
 * @param  pData: 指向要写入数据的指针。
 * @param  Size: 要写入的数据长度（以字节为单位）。
 * @retval None
 */
void W25Q64_WriteBytes(uint32_t Address, const uint8_t* pData, uint32_t Size)
{
    while (Size > 0)
    {
        uint32_t page_offset = Address % W25Q64_PAGE_SIZE;
        uint32_t chunk = W25Q64_PAGE_SIZE - page_offset;
        uint8_t cmd[4];

        if (chunk > Size)
        {
            chunk = Size;
        }

        W25Q64_WriteEnable();
        cmd[0] = 0x02; // page program
        cmd[1] = (Address >> 16) & 0xFF;
        cmd[2] = (Address >> 8) & 0xFF;
        cmd[3] = Address & 0xFF;

        HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&hspi3, (uint8_t*)pData, chunk, HAL_MAX_DELAY);
        HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET);
        W25Q64_WaitForWriteEnd();

        Address += chunk;
        pData += chunk;
        Size -= chunk;
    }
}

/**
 * @brief  擦除指定扇区。
 * @param  SectorNum: 扇区号。
 * @retval None
 */
void W25Q64_EraseSector(uint32_t SectorNum)
{
    uint8_t cmd[4];
    uint32_t addr = SectorNum * W25Q64_SECTOR_SIZE;

    W25Q64_WriteEnable();
    cmd[0] = 0x20; // sector erase 4KB
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi3, cmd, 4, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_PORT, W25Q64_CS_PIN, GPIO_PIN_SET);
    W25Q64_WaitForWriteEnd();
}
