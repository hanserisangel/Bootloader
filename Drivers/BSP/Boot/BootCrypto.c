#include "BootCrypto.h"
#include <string.h>
#include "main.h"
#include "BootOta.h"
#include "mbedtls/aes.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

// Replace with the device ECDH private key (P-256), 32 bytes.
static const uint8_t k_ota_ecdh_priv[32] = {
    0x00
};

/**
 * @brief  从元数据中派生 AES 密钥，使用 ECDH 公钥和盐作为输入。
 * @param  key: 输出缓冲区，用于存储派生的 AES 密钥。
 * @param  key_len: 输出缓冲区的长度（以字节为单位），例如 16 表示 128-bit AES。
 * @param  meta: 输入的元数据，包含 ECDH 公钥、盐和 IV。
 * @retval true 表示成功派生 AES 密钥，false 表示失败。
 */
static bool Boot_DeriveAesKey(uint8_t *key, size_t key_len, const uint8_t *meta)
{
    const uint8_t *eph_pub = meta;
    const uint8_t *salt = meta + OTA_ECDH_PUB_LEN;
    uint8_t secret[64];     // ECDH 共享秘密，P-256 的输出长度为 32 字节，但预留空间以防万一
    size_t secret_len = 0;
    int ret = 0;

    mbedtls_ecdh_context ctx;   // ECDH 上下文
    mbedtls_ecdh_init(&ctx);

    // 1. 使用 ECDH 计算共享秘密，使用设备的私钥和元数据中的公钥
    ret = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    if(ret != 0)
        goto cleanup;

    ret = mbedtls_mpi_read_binary(&ctx.d, k_ota_ecdh_priv, sizeof(k_ota_ecdh_priv));
    if(ret != 0)
        goto cleanup;

    ret = mbedtls_ecp_point_read_binary(&ctx.grp, &ctx.Qp, eph_pub, OTA_ECDH_PUB_LEN);
    if(ret != 0)
        goto cleanup;

    // 计算共享秘密，结果存储在 secret 中，长度存储在 secret_len 中
    ret = mbedtls_ecdh_calc_secret(&ctx, &secret_len, secret, sizeof(secret), NULL, NULL);
    if(ret != 0)
        goto cleanup;

    {
        // 2. 使用 HKDF 派生 AES 密钥，使用共享秘密作为输入密钥材料，盐来自元数据，info 固定为 "OTA-AES"
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if(md == NULL)
        {
            ret = -1;
            goto cleanup;
        }
        // 派生 AES 密钥，输出到 key 中，长度为 key_len
        ret = mbedtls_hkdf(md, salt, OTA_SALT_LEN,
            secret, secret_len,
            (const unsigned char *)"OTA-AES", 7,
            key, key_len);
        if(ret != 0)
            goto cleanup;
    }

cleanup:
    mbedtls_platform_zeroize(secret, sizeof(secret));
    mbedtls_ecdh_free(&ctx);
    return (ret == 0);
}

/**
 * @brief  从 W25Q64 读取数据并解密到 MCU Flash。(AES-CTR 模式)
 * @param  cipher_addr: W25Q64 中加密固件的起始地址。
 * @param  fw_size: 固件大小（以字节为单位）。
 * @param  flash_addr: MCU Flash 中存储解密后数据的起始地址。
 * @retval None
 */
bool Boot_DecryptW25Q64ToMcu(uint32_t cipher_addr, uint32_t fw_size, uint32_t flash_addr)
{
    uint8_t meta[OTA_META_LEN]; // 存储从 W25Q64 读取的元数据（公钥、盐、IV）
    uint8_t key[16];            // 128-bit AES 密钥
    const uint8_t *iv;
    mbedtls_aes_context aes;    // AES 上下文
    size_t nc_off = 0;          // AES-CTR 模式的偏移量，初始为 0
    uint8_t stream_block[16];   // AES-CTR 模式的流块缓冲区
    uint8_t nonce_counter[16];  // AES-CTR 模式的 nonce+counter，初始值为 IV，后续会自动递增

    // 1. 从 W25Q64 读取元数据
    Boot_ReadW25Q64Bytes(OTA_META_ADDR, meta, OTA_META_LEN);

    // 2. 派生 AES 密钥，使用 ECDH 公钥和盐作为输入
    if(!Boot_DeriveAesKey(key, sizeof(key), meta))
        return false;

    iv = meta + OTA_ECDH_PUB_LEN + OTA_SALT_LEN;    // IV 紧跟在公钥和盐之后
    memcpy(nonce_counter, iv, sizeof(nonce_counter));   // AES-CTR 的 nonce+counter 初始值为 IV
    memset(stream_block, 0, sizeof(stream_block));

    // 3. 使用 AES-CTR 模式解密固件数据并写入 MCU Flash
    mbedtls_aes_init(&aes);
    if(mbedtls_aes_setkey_enc(&aes, key, 128) != 0)
    {
        mbedtls_aes_free(&aes);
        mbedtls_platform_zeroize(key, sizeof(key));
        return false;
    }

    uint32_t offset = 0;
    while(offset < fw_size)
    {
        uint32_t chunk = fw_size - offset;
        if(chunk > UPDATA_BUFF)
            chunk = UPDATA_BUFF;

        static uint8_t cipher_buf[UPDATA_BUFF];    // 存储从 W25Q64 读取的加密数据
        static uint8_t plain_buf[UPDATA_BUFF + 4]; // 存储解密后的数据，预留 4 字节以处理可能的填充

        // 从 W25Q64 读取加密数据
        Boot_ReadW25Q64Bytes(cipher_addr + offset, cipher_buf, chunk);
        
        // 使用 AES-CTR 模式解密数据
        if(mbedtls_aes_crypt_ctr(&aes, chunk, &nc_off, nonce_counter, stream_block,
                cipher_buf, plain_buf) != 0)    // AES-CTR 解密
        {
            mbedtls_aes_free(&aes);
            mbedtls_platform_zeroize(key, sizeof(key));
            return false;
        }

        // 将解密后的数据写入 MCU Flash，注意处理最后一个块的填充
        uint32_t padded = (chunk + 3U) & ~3U;
        if(padded > chunk)
            memset(plain_buf + chunk, 0xFF, padded - chunk);

        // 写入 Flash，假设 MCU_WriteFlash 的地址必须是 4 字节对齐的，并且长度也是 4 字节的倍数
        MCU_WriteFlash(flash_addr + offset, (uint32_t *)plain_buf, padded / 4U);
        offset += chunk;
    }

    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(key, sizeof(key));
    return true;
}
