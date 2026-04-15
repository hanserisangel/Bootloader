#ifndef __BOOT_CRYPTO_H__
#define __BOOT_CRYPTO_H__

#include <stdint.h>
#include <stdbool.h>

bool Boot_DecryptW25Q64ToMcu(uint32_t cipher_addr, uint32_t fw_size, uint32_t flash_addr);

#endif /* __BOOT_CRYPTO_H__ */
