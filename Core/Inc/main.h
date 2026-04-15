/* USER CODE BEGIN Header */

/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

#include "Log.h"
#include "Flash.h"
#include "Uart.h"
#include "W25Q64.h"
#include "Boot.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI_CS_Pin GPIO_PIN_3
#define SPI_CS_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */
#define MCU_FLASH_TOTAL_SIZE        1024U         // 总共 1MB 的Flash
#define MCU_FLASH_TOTAL_PAGE        12U           // 总共 12 个页
#define MCU_FLASH_B_PAGE_SIZE       64U         // flash 的B区大小为 64KB
#define MCU_FLASH_A_PAGE_SIZE       (MCU_FLASH_TOTAL_SIZE - MCU_FLASH_B_PAGE_SIZE) // 剩下的都是A区
#define MCU_FLASH_A_START_PAGE      4U      // A 区从第 4 页开始
#define MCU_FLASH_A_PAGE_NUM        (8 - MCU_FLASH_A_START_PAGE) // A 区总页数

#define MCU_FLASH_A_START_ADDRESS  (FLASH_BASE + MCU_FLASH_B_PAGE_SIZE * 1024U) // A区起始地址

#define OTA_FLAG                  0xAABB1122

// OTA header + signature/public key parameters (override as needed)
#define OTA_HDR_MAGIC             0x4F544148U // "OTAH" little-endian
#define OTA_HDR_SIZE              16U
#define OTA_SIG_MAX               96U     // DER ECDSA(P-256) max size is typically <= 72
#define OTA_PUBKEY_LEN            125U    // P-256 公钥长度为 64 字节，但可能会有额外的头部信息，预留 125 字节

#define OTA_ECDH_PUB_LEN           65U
#define OTA_SALT_LEN               16U
#define OTA_IV_LEN                 16U
#define OTA_META_LEN               (OTA_ECDH_PUB_LEN + OTA_SALT_LEN + OTA_IV_LEN)

// W25Q64 分区表
#define OTA_PUBKEY_ADDR           0U      // 公钥，地址 0 开始，4kB 的空间（1个扇区）
#define OTA_INFO_ADDR             4096U   // OTA 信息，地址 4096 开始，4kB 的空间（1个扇区）
#define OTA_HEAT_ADDR             (OTA_INFO_ADDR + 4096U)   // 断点续传热数据，地址 8192 开始，56KB 字节的空间
#define OTA_META_ADDR             OTA_HEAT_ADDR
#define OTA_Firmware_A_ADDR       0x10000U // 固件A区，地址 0x10000 开始，512KB 的空间
#define OTA_Firmware_B_ADDR       (OTA_Firmware_A_ADDR + 512 * 1024) // 固件B区，地址 0x90000 开始，512KB 的空间

// OTA header 结构体定义
typedef struct{
  uint32_t magic;       // OTA_HDR_MAGIC
  uint32_t header_size; // OTA_HDR_SIZE
  uint32_t fw_size;     // firmware size in bytes
  uint32_t sig_len;     // signature length in bytes
}OTA_Header_t;

typedef struct{
  uint32_t OTA_Flag;
  uint32_t FileSize;        // 服务器下发的整个应用程序的大小（字节）
  uint8_t OTA_version[12];  // OTA 版本号，字符串数组，格式: version-1.0
  uint8_t OTA_area;          // 0 表示 A 区，1 表示 B 区
}OTA_Info_t;
extern OTA_Info_t OTA_Info;

#define UPDATA_BUFF                  1024
typedef struct{
  uint8_t UpAppBuffer[UPDATA_BUFF];   // 一次写入的应用程序大小
  uint16_t Ymodem_Timer;              // Ymodem 协议发送时限
  uint16_t Ymodem_CRC;                // Ymodem 协议接收的 CRC 校验码
  uint32_t Ymodem_TotalReceived;      // 已接收的有效数据字节数
  uint32_t Ymodem_WriteBlockIndex;    // 已写入的 1K 块数量
  uint16_t Ymodem_BytesInBuffer;      // 当前缓存的有效字节数
  uint8_t Ymodem_ExpectBlock;         // 期望的数据块号
  uint8_t Ymodem_HeaderReceived;      // 是否已接收头包
}UpData_A_t;
extern UpData_A_t UpData_A;

typedef enum{
  UART_CONSOLE_IDLE = 0,
  IAP_YMODEM_START,
  IAP_YMODEM_RECEIVED,
  IAP_YMODEM_END,
  SET_VERSION,
  UPDATA_A_SET,
}OTA_State_t;
extern OTA_State_t OTA_state;

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
