#ifndef __BOOT_H__
#define __BOOT_H__

typedef void (*load_a)(void); // 定义函数指针

#define     SOH     0x01 // 数据包头 (128 bytes)
#define     STX     0x02 // 数据包头 (1K bytes)
#define     EOT     0x04 // 发送结束
#define     ACK     0x06 // 认可应答
#define     NAK     0x15 // 不认可应答
#define     CRC_REQ 'C'  // 请求 CRC 模式

void BootLoader_Brance(void);
void BootLoader_Event(uint8_t* pdata);
void BootLoader_State(void);
bool Boot_VerifySignature(void);

#endif /* __BOOT_H__ */
