/**
 * @file    Log.h
 * @brief   系统日志宏定义与打印接口
 * @date    2025-12-07
 */

#ifndef __LOG_H__
#define __LOG_H__

#include "Uart.h"

// 定义日志开关 (1: 开启, 0: 关闭)
#define LOG_ENABLE      1

/*__VA_ARGS__：C 语言预编译器的可变参数宏扩展符
    将宏调用时传入的...对应的所有参数，原样传递给printf的可变参数列表
    处理「可变参数为空」的场景：若宏调用时未传入额外参数，##会消除printf格式串后多余的逗号，避免编译报错
    LOG_I("system init ok")，若直接写__VA_ARGS__，此时会变成printf("[INFO] system init ok\r\n", )，逗号多余导致编译失败
*/ 
#if LOG_ENABLE
// 普通日志
#define LOG_I(fmt, ...) Uart_Printf("[INFO] " fmt "\r\n", ##__VA_ARGS__)
// 警告日志
#define LOG_W(fmt, ...) Uart_Printf("[WARNING] " fmt  "\r\n", ##__VA_ARGS__)
// 错误日志
#define LOG_E(fmt, ...) Uart_Printf("[ERROR] " fmt "\r\n", ##__VA_ARGS__)
// DEBUG日志
#define LOG_D(fmt, ...) Uart_Printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__)

#else
// 关闭时，宏展开为空，完全不占代码空间
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)
#define LOG_RAW(fmt, ...)
#endif

#endif /* __LOG_H__ */
