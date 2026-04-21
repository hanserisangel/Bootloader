#ifndef __BOOT_CRYPTO_H__
#define __BOOT_CRYPTO_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool Boot_DeriveAesKey(uint8_t *key, size_t key_len, const uint8_t *meta);

bool Boot_DecryptW25Q64ToMcu(uint32_t cipher_addr, uint32_t fw_size, uint32_t flash_addr);
// bool Boot_DecryptW25Q64ToW25Q64(uint32_t cipher_addr, uint32_t fw_size, uint32_t out_addr);

#endif /* __BOOT_CRYPTO_H__ */
