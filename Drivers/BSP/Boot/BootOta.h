#ifndef __BOOT_OTA_H__
#define __BOOT_OTA_H__

#include <stdint.h>
#include <stdbool.h>
#include "mbedtls/sha256.h"
#include "main.h"

typedef struct {
    mbedtls_sha256_context sha_ctx;
    bool sha_started;
    uint32_t firmware_size;

    uint32_t sig_received;
    uint32_t sig_len;
    uint32_t payload_received;
    uint32_t hdr_received;
    uint32_t meta_received;
    
    uint8_t hdr_buf[OTA_HDR_SIZE];  // 存储从 OTA 头部接收的数据，直到接收完整个头部为止
    uint8_t meta_buf[OTA_META_LEN]; // 存储从 OTA 头部之后接收的元数据（公钥、盐、IV）
    uint8_t sig_buf[OTA_SIG_MAX];   // 存储从 OTA 头部之后接收的签名数据，直到接收完整个签名为止
} BootOtaContext;

extern BootOtaContext g_ota;

void Boot_ResetVerifyState(void);
void Boot_ReadW25Q64Bytes(uint32_t addr, uint8_t *out, uint32_t len);
bool Boot_ParseOtaHeader(const uint8_t *buf, uint32_t len, OTA_Header_t *out);
bool Boot_VerifySignature(void);

#endif /* __BOOT_OTA_H__ */
