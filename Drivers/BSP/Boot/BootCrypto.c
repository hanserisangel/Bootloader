#include "BootCrypto.h"
#include <string.h>
#include "main.h"
#include "rng.h"
#include "BootOta.h"
#include "mbedtls/aes.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

static void Boot_PrintHex16(const char *tag, const uint8_t *buf, size_t len)
{
    size_t n = (len < 16U) ? len : 16U;
    Uart_Printf("%s:", tag);
    for(size_t i = 0; i < n; i++)
        Uart_Printf(" %02X", buf[i]);
    Uart_Printf("\r\n");
}

// Replace with the device ECDH private key (P-256), 32 bytes.
static const uint8_t k_ota_ecdh_priv[] = {
    0x0e, 0xa7, 0xb4, 0xed, 0x04, 0x45, 0xe8, 0x1e, 0x3e, 0xf8, 0x79, 0x98, 0x1c, 0x3f, 0x18, 0xd3,
    0xe0, 0xf9, 0x7e, 0x39, 0x14, 0x15, 0x8a, 0xb1, 0xb7, 0xf5, 0xd2, 0xab, 0x55, 0x74, 0x28, 0xd8
};

static int Boot_EcdhRng(void *ctx, unsigned char *output, size_t len)
{
    (void)ctx;
    size_t offset = 0;

    while(offset < len)
    {
        uint32_t rnd = 0;
        size_t n = len - offset;

        if(HAL_RNG_GenerateRandomNumber(&hrng, &rnd) != HAL_OK)
            return MBEDTLS_ERR_ECP_RANDOM_FAILED;

        if(n > sizeof(rnd))
            n = sizeof(rnd);

        memcpy(output + offset, &rnd, n);
        offset += n;
    }

    return 0;
}

/**
 * @brief  从元数据中派生 AES 密钥，使用 ECDH 公钥和盐作为输入。
 * @param  key: 输出缓冲区，用于存储派生的 AES 密钥。
 * @param  key_len: 输出缓冲区的长度（以字节为单位），例如 16 表示 128-bit AES。
 * @param  meta: 输入的元数据，包含 ECDH 公钥、盐和 IV。
 * @retval true 表示成功派生 AES 密钥，false 表示失败。
 */
bool Boot_DeriveAesKey(uint8_t *key, size_t key_len, const uint8_t *meta)
{
    const uint8_t *eph_pub = meta;
    const uint8_t *salt = meta + OTA_ECDH_PUB_LEN;
    uint8_t secret[64];     // ECDH 共享秘密，P-256 的输出长度为 32 字节，但预留空间以防万一
    uint8_t secret32[32];
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

    ret = mbedtls_ecp_check_privkey(&ctx.grp, &ctx.d);
    if(ret != 0)
        goto cleanup;

    if(eph_pub[0] != 0x04)
    {
        ret = MBEDTLS_ERR_ECP_INVALID_KEY;
        goto cleanup;
    }

    ret = mbedtls_ecp_point_read_binary(&ctx.grp, &ctx.Qp, eph_pub, OTA_ECDH_PUB_LEN);
    if(ret != 0)
        goto cleanup;

    ret = mbedtls_ecp_check_pubkey(&ctx.grp, &ctx.Qp);
    if(ret != 0)
        goto cleanup;

    // 计算共享秘密，结果存储在 secret 中，长度存储在 secret_len 中
    ret = mbedtls_ecdh_calc_secret(&ctx, &secret_len, secret, sizeof(secret), Boot_EcdhRng, NULL);
    if(ret != 0)
        goto cleanup;

    if(secret_len > sizeof(secret32))
    {
        ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        goto cleanup;
    }
    memset(secret32, 0, sizeof(secret32));
    memcpy(secret32 + sizeof(secret32) - secret_len, secret, secret_len);

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
            secret32, sizeof(secret32),
            (const unsigned char *)"OTA-AES", 7,
            key, key_len);
        if(ret != 0)
            goto cleanup;
    }

cleanup:
    if(ret != 0)
        LOG_E("Boot_DeriveAesKey failed, ret=%d", ret);
    mbedtls_platform_zeroize(secret32, sizeof(secret32));
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
    // bool probe_printed = false;

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

        // if(!probe_printed)
        // {
        //     Boot_PrintHex16("DBG cipher[0:16]", cipher_buf, chunk);
        //     Boot_PrintHex16("DBG key[0:16]", key, sizeof(key));
        //     Boot_PrintHex16("DBG iv[0:16]", iv, OTA_IV_LEN);
        //     Boot_PrintHex16("DBG plain[0:16]", plain_buf, chunk);
        //     probe_printed = true;
        // }

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

// /**
//  * @brief  从 W25Q64 读取数据并解密到 W25Q64。(AES-CTR 模式)
//  * @param  cipher_addr: W25Q64 中加密固件的起始地址。
//  * @param  fw_size: 固件大小（以字节为单位）。
//  * @param  out_addr: W25Q64 中存储解密后数据的起始地址。
//  * @retval None
//  */
// bool Boot_DecryptW25Q64ToW25Q64(uint32_t cipher_addr, uint32_t fw_size, uint32_t out_addr)
// {
//     uint8_t meta[OTA_META_LEN];
//     uint8_t key[16];
//     const uint8_t *iv;
//     mbedtls_aes_context aes;
//     size_t nc_off = 0;
//     uint8_t stream_block[16];
//     uint8_t nonce_counter[16];

//     Boot_ReadW25Q64Bytes(OTA_META_ADDR, meta, OTA_META_LEN);

//     if(!Boot_DeriveAesKey(key, sizeof(key), meta))
//         return false;

//     iv = meta + OTA_ECDH_PUB_LEN + OTA_SALT_LEN;
//     memcpy(nonce_counter, iv, sizeof(nonce_counter));
//     memset(stream_block, 0, sizeof(stream_block));

//     mbedtls_aes_init(&aes);
//     if(mbedtls_aes_setkey_enc(&aes, key, 128) != 0)
//     {
//         mbedtls_aes_free(&aes);
//         mbedtls_platform_zeroize(key, sizeof(key));
//         return false;
//     }

//     // 3. 解密固件数据并写入 W25Q64 的另一个区域
//     for(uint32_t offset = 0; offset < fw_size; )
//     {
//         uint32_t chunk = fw_size - offset;
//         static uint8_t cipher_buf[UPDATA_BUFF];
//         static uint8_t plain_buf[UPDATA_BUFF];

//         if(chunk > UPDATA_BUFF)
//             chunk = UPDATA_BUFF;

//         Boot_ReadW25Q64Bytes(cipher_addr + offset, cipher_buf, chunk);

//         if(mbedtls_aes_crypt_ctr(&aes, chunk, &nc_off, nonce_counter, stream_block,
//             cipher_buf, plain_buf) != 0)
//         {
//             mbedtls_aes_free(&aes);
//             mbedtls_platform_zeroize(key, sizeof(key));
//             return false;
//         }

//         // 写入解密后的数据到 W25Q64
//         W25Q64_WriteBytes(out_addr + offset, plain_buf, chunk);
//         offset += chunk;
//     }

//     mbedtls_aes_free(&aes);
//     mbedtls_platform_zeroize(key, sizeof(key));
//     return true;
// }
