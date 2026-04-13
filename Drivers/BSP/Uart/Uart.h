#ifndef __UART_H__
#define __UART_H__

/**
 * @file    Uart.h
 * @brief   STM32 通用 UART 驱动接口
 * @note    支持 DMA 循环接收 + 空闲中断 (IDLE) + 环形(块状)缓冲区
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define RX_BUF_SIZE 4096  // 缓冲区大小
#define RX_DATA_CELLING 1030 // 单次接收的数据包最大字节数 (YMODEM 1K: 1029)
#define RX_BLOCK_NUM 4 // 缓冲区中可含有的块的最大数

// 环形缓冲区的块
typedef struct{
    uint16_t start; // 指向块的起始字节
    uint16_t end; // 指向块的终止字节
}Block;

struct ringbuffer{
    Block buffer[RX_BLOCK_NUM];
    uint16_t r; // 块读指针
    uint16_t w; // 块写指针
    uint16_t BlockCount; // 当前缓冲区存储的块数
    uint16_t TypeCount; // 标记目前所在缓冲区位置
};
typedef struct ringbuffer* ringbuffer_t;

extern ringbuffer_t uart_rb;

/**
 * @brief  初始化 UART 外设
 * @param  rb: 指向环形缓冲区的指针
 * @retval None
 */
void Uart_Init(ringbuffer_t rb);

/**
 * @brief  重定向输出到 UART
 * @note   类似 printf，但输出到 UART
 * @param  fmt: 格式字符串
 * @retval None
 */
void Uart_Printf(const char *fmt, ...);

/**
 * @brief  将一个字节存入环形缓冲区 (非阻塞)
 * @param  rb: 指向环形缓冲区的指针
 * @param  data: 要存入的数据
 * @retval None
 */
void RingBuffer_Put(ringbuffer_t rb, uint8_t* data, uint16_t length);

/**
 * @brief  从环形缓冲区读取一个字节 (非阻塞)
 * @note   非阻塞读取单个字节
 * @param  rb: 指向环形缓冲区的指针
 * @retval data: 读取到的数据
 * @retval 0: 缓冲区为空，读取失败
 */
uint16_t RingBuffer_Get(ringbuffer_t rb, uint8_t* data);

/**
 * @brief  清空环形缓冲区
 * @note   复位读写指针，丢弃当前缓冲区内所有未读数据。
 * @param  rb: 指向环形缓冲区的指针
 * @retval None
 */
void RingBuffer_Clear(ringbuffer_t rb);

#endif /* __UART_H__ */
