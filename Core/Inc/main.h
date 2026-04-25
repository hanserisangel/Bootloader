#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f4xx_hal.h"
#include "dma.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "rng.h"

#include "mbedtls.h"
#include "Log.h"
#include "Flash.h"
#include "Uart.h"
#include "W25Q64.h"
#include "Boot.h"

void Error_Handler(void);

#define SPI_CS_Pin GPIO_PIN_3
#define SPI_CS_GPIO_Port GPIOG

/*
 * Bootloader feature trim profiles (set one of these to 1, others keep 0):
 * 1) BOOT_CFG_TRIM_NO_CONSOLE: disable UART command console only.
 * 2) BOOT_CFG_TRIM_OTA_DELTA_ONLY: keep OTA(auto) + delta upgrade only.
 * 3) BOOT_CFG_TRIM_OTA_FULL_ONLY: keep OTA(auto) + full upgrade only.
 * 4) BOOT_CFG_TRIM_LOCAL_DELTA_ONLY: keep local(UART) + delta upgrade only.
 * 5) BOOT_CFG_TRIM_LOCAL_FULL_ONLY: keep local(UART) + full upgrade only.
 */
#define BOOT_CFG_TRIM_NO_CONSOLE         0
#define BOOT_CFG_TRIM_OTA_DELTA_ONLY     0
#define BOOT_CFG_TRIM_OTA_FULL_ONLY      0
#define BOOT_CFG_TRIM_LOCAL_DELTA_ONLY   0
#define BOOT_CFG_TRIM_LOCAL_FULL_ONLY    0

#if ((BOOT_CFG_TRIM_NO_CONSOLE + BOOT_CFG_TRIM_OTA_DELTA_ONLY + BOOT_CFG_TRIM_OTA_FULL_ONLY + \
  BOOT_CFG_TRIM_LOCAL_DELTA_ONLY + BOOT_CFG_TRIM_LOCAL_FULL_ONLY) > 1)
#error "Only one BOOT_CFG_TRIM_* profile can be enabled at a time."
#endif

#if BOOT_CFG_TRIM_NO_CONSOLE
#define BOOT_USE_CONSOLE      0
#define BOOT_USE_LOCAL        1
#define BOOT_USE_OTA_AUTO     1
#define BOOT_USE_FULL         1
#define BOOT_USE_DELTA        1
#elif BOOT_CFG_TRIM_OTA_DELTA_ONLY
#define BOOT_USE_CONSOLE      0
#define BOOT_USE_LOCAL        0
#define BOOT_USE_OTA_AUTO     1
#define BOOT_USE_FULL         0
#define BOOT_USE_DELTA        1
#elif BOOT_CFG_TRIM_OTA_FULL_ONLY
#define BOOT_USE_CONSOLE      0
#define BOOT_USE_LOCAL        0
#define BOOT_USE_OTA_AUTO     1
#define BOOT_USE_FULL         1
#define BOOT_USE_DELTA        0
#elif BOOT_CFG_TRIM_LOCAL_DELTA_ONLY
#define BOOT_USE_CONSOLE      1
#define BOOT_USE_LOCAL        1
#define BOOT_USE_OTA_AUTO     0
#define BOOT_USE_FULL         0
#define BOOT_USE_DELTA        1
#elif BOOT_CFG_TRIM_LOCAL_FULL_ONLY
#define BOOT_USE_CONSOLE      1
#define BOOT_USE_LOCAL        1
#define BOOT_USE_OTA_AUTO     0
#define BOOT_USE_FULL         1
#define BOOT_USE_DELTA        0
#else
#define BOOT_USE_CONSOLE      1
#define BOOT_USE_LOCAL        1
#define BOOT_USE_OTA_AUTO     1
#define BOOT_USE_FULL         1
#define BOOT_USE_DELTA        1
#endif

#define MCU_FLASH_APP_A_SLOT        0U
#define MCU_FLASH_APP_B_SLOT        1U
#define MCU_FLASH_APP_A_ADDR        (FLASH_BASE + 0x20000U) // A区起始地址
#define MCU_FLASH_APP_B_ADDR        (FLASH_BASE + 0x80000U) // B区起始地址
#define MCU_FLASH_APP_A_SECTOR      5U
#define MCU_FLASH_APP_A_COUNT       3U
#define MCU_FLASH_APP_B_SECTOR      8U
#define MCU_FLASH_APP_B_COUNT       3U
#define MCU_FLASH_SLOT_SIZE         (384U * 1024U)

#define OTA_FLAG                  0xAABB1122

#define OTA_HDR_MAGIC             0x4F544148U
#define OTA_HDR_SIZE              20U
#define OTA_SIG_MAX               96U
#define OTA_PUBKEY_LEN            91U    // P-256 公钥长度为 64 字节，但可能会有额外的头部信息，预留 91 字节
#define OTA_PKG_TYPE_FULL         0U
#define OTA_PKG_TYPE_DELTA        1U

#define OTA_ECDH_PUB_LEN           65U
#define OTA_SALT_LEN               16U
#define OTA_IV_LEN                 16U
#define OTA_META_LEN               (OTA_ECDH_PUB_LEN + OTA_SALT_LEN + OTA_IV_LEN)
#define OTA_VERSION_MAX_LEN         12

// W25Q64 分区表
/* 第一扇区 0~4KB */
#define OTA_PUBKEY_ADDR           0U
/* 第二扇区 4KB~8KB */
#define OTA_INFO_ADDR             (OTA_PUBKEY_ADDR + 4096U)
/* 第三扇区 8KB~12KB */
#define OTA_META_ADDR             (OTA_INFO_ADDR + 4096U)
/* 第四扇区 12KB~16KB */
#define OTA_HDR_ADDR              (OTA_META_ADDR + 4096U)
#define OTA_SIG_ADDR              (OTA_HDR_ADDR + OTA_HDR_SIZE) 
/* 第五扇区到第二块 16KB~64KB */
#define OTA_HEAT_ADDR             (OTA_HDR_ADDR + 4096U) 

/* 第二块*/
#define OTA_STAGING_ADDR          0x100000U
#define OTA_STAGING_SIZE          (384U * 1024U)
#define OTA_TUZ_DICT_MAX          (2U * 1024U)
#define OTA_TUZ_CACHE_SIZE        1024U


// OTA header 结构体定义
typedef struct{
  uint32_t magic;       // OTA_HDR_MAGIC
  uint32_t header_size; // OTA_HDR_SIZE
  uint32_t pkg_type;    // 0: full, 1: delta
  uint32_t fw_size;     // firmware size in bytes
  uint32_t sig_len;     // signature length in bytes
}OTA_Header_t;

typedef enum {
  UPDATE = 0,      // OTA包已接收但未验证
  NORMAL,
  FAIL
}OTA_status_t;

typedef struct{
  uint32_t OTA_Flag;
  uint32_t FileSize;        // 服务器下发的整个应用程序的大小（字节）
  uint8_t OTA_version[OTA_VERSION_MAX_LEN];  // OTA 版本号，字符串数组，格式: version-1.0
  uint8_t OTA_area;          // 0 表示 A 区，1 表示 B 区
  uint8_t OTA_type;          // 0 表示全量更新，1 表示增量更新
  OTA_status_t OTA_status;        // OTA 状态，用来自动回滚
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
}Local_UpDate_t;
extern Local_UpDate_t Local_UpDate;

typedef enum{
  UART_CONSOLE_IDLE = 0,
  IAP_YMODEM_START,
  IAP_YMODEM_RECEIVED,
  SET_VERSION,
  UPDATA_FULL_SET,    // 全量更新状态
  UPDATA_DELTA_SET,   // 增量更新状态
}OTA_State_t;
extern OTA_State_t OTA_state;

#endif /* __MAIN_H */
