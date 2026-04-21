#ifndef __BOOT_DELTA_H__
#define __BOOT_DELTA_H__

#include <stdint.h>
#include <stdbool.h>

// Delta format: HPatchLite create_lite_diff() output.
// Input stream is OTA payload ciphertext from W25Q64 staging area.
// Runtime pipeline: AES-CTR decrypt stream -> (optional tinyuz decompress) -> HPatchLite apply.
bool Boot_ApplyDeltaFromW25Q64(uint32_t delta_addr,
    uint32_t delta_size,
    uint32_t base_addr,
    uint32_t target_addr);

#endif /* __BOOT_DELTA_H__ */
