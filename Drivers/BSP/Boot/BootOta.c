#include "BootOta.h"
#include <string.h>
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
// #include "mbedtls/error.h"

BootOtaContext g_ota;   // 全局 OTA 上下文，存储 OTA 验证状态和相关数据

/**
 * @brief  重置 OTA 验证状态，清除所有相关数据和状态标志。
 * @param  None
 * @retval None
 */
void Boot_ResetVerifyState(void)
{
    if(g_ota.sha_started)
        mbedtls_sha256_free(&g_ota.sha_ctx);
    g_ota.sha_started = false;
    g_ota.firmware_size = 0;
    g_ota.sig_received = 0;
    g_ota.sig_len = 0;
    g_ota.payload_received = 0;
    g_ota.hdr_received = 0;
    g_ota.meta_received = 0;
    mbedtls_platform_zeroize(g_ota.hdr_buf, sizeof(g_ota.hdr_buf));
    mbedtls_platform_zeroize(g_ota.meta_buf, sizeof(g_ota.meta_buf));
    mbedtls_platform_zeroize(g_ota.sig_buf, sizeof(g_ota.sig_buf));
}

/**
 * @brief  从 W25Q64 读取数据。
 * @param  addr: W25Q64 中数据的起始地址。
 * @param  out: 指向存储读取数据的缓冲区。
 * @param  len: 要读取的数据长度（以字节为单位）。
 * @retval None
 */
void Boot_ReadW25Q64Bytes(uint32_t addr, uint8_t *out, uint32_t len)
{
    while(len > 0)
    {
        uint16_t page = (uint16_t)(addr / W25Q64_PAGE_SIZE);
        uint32_t offset = addr % W25Q64_PAGE_SIZE;
        uint32_t chunk = W25Q64_PAGE_SIZE - offset;
        if(chunk > len)
            chunk = len;

        if(offset == 0 && chunk == W25Q64_PAGE_SIZE)
        {
            W25Q64_ReadData(page, out, chunk);
        }
        else
        {
            static uint8_t temp[W25Q64_PAGE_SIZE];
            W25Q64_ReadData(page, temp, W25Q64_PAGE_SIZE);
            memcpy(out, temp + offset, chunk);
        }

        addr += chunk;
        out += chunk;
        len -= chunk;
    }
}

/**
 * @brief  解析 OTA 头部数据，提取固件大小和签名长度等信息。
 * @param  buf: 指向存储 OTA 头部数据的缓冲区。
 * @param  len: OTA 头部数据的长度（以字节为单位）。
 * @param  out: 指向存储解析结果的 OTA_Header_t 结构体。
 * @retval true 解析成功，false 解析失败（例如头部格式错误）。
 */
bool Boot_ParseOtaHeader(const uint8_t *buf, uint32_t len, OTA_Header_t *out)
{
    if(len < OTA_HDR_SIZE || out == NULL)
        return false;

    uint32_t magic = (uint32_t)buf[0] |
        ((uint32_t)buf[1] << 8) |
        ((uint32_t)buf[2] << 16) |
        ((uint32_t)buf[3] << 24);
    uint32_t header_size = (uint32_t)buf[4] |
        ((uint32_t)buf[5] << 8) |
        ((uint32_t)buf[6] << 16) |
        ((uint32_t)buf[7] << 24);
    uint32_t pkg_type = (uint32_t)buf[8] |
        ((uint32_t)buf[9] << 8) |
        ((uint32_t)buf[10] << 16) |
        ((uint32_t)buf[11] << 24);
    uint32_t fw_size = (uint32_t)buf[12] |
        ((uint32_t)buf[13] << 8) |
        ((uint32_t)buf[14] << 16) |
        ((uint32_t)buf[15] << 24);
    uint32_t sig_len = (uint32_t)buf[16] |
        ((uint32_t)buf[17] << 8) |
        ((uint32_t)buf[18] << 16) |
        ((uint32_t)buf[19] << 24);

    if(magic != OTA_HDR_MAGIC || header_size != OTA_HDR_SIZE)
        return false;
    if(pkg_type != OTA_PKG_TYPE_FULL && pkg_type != OTA_PKG_TYPE_DELTA)
        return false;

    out->magic = magic;
    out->header_size = header_size;
    out->pkg_type = pkg_type;
    out->fw_size = fw_size;
    out->sig_len = sig_len;
    return true;
}

/**
 * @brief  验证接收的固件数据的签名是否正确。
 * @note   该函数会计算接收数据的 SHA-256 摘要，并使用存储在 W25Q64 中的公钥验证签名。
 * @retval true 验证成功，false 验证失败（例如签名不匹配或验证过程中发生错误）。
 */
bool Boot_VerifySignature(void)
{
    static uint8_t digest[32];      // 存储 SHA-256 摘要的缓冲区
    static uint8_t pubkey_buf[OTA_PUBKEY_LEN];
    mbedtls_pk_context pk;
    int ret = 0;

    if(!g_ota.sha_started)
        return false;
    if(g_ota.sig_len == 0 || g_ota.sig_received < g_ota.sig_len)
        return false;

    mbedtls_sha256_finish(&g_ota.sha_ctx, digest);
    mbedtls_sha256_free(&g_ota.sha_ctx);
    g_ota.sha_started = false;

    Boot_ReadW25Q64Bytes(OTA_PUBKEY_ADDR, pubkey_buf, OTA_PUBKEY_LEN);

    mbedtls_pk_init(&pk);
    ret = mbedtls_pk_parse_public_key(&pk, pubkey_buf, OTA_PUBKEY_LEN);
    if(ret != 0)
    {
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, g_ota.sig_buf, g_ota.sig_len);
    // LOG_E("Signature verify result: %d", ret);

    mbedtls_pk_free(&pk);

    mbedtls_platform_zeroize(digest, sizeof(digest));
    return (ret == 0);
}

bool Boot_VerifySignatureFromW25Q64(OTA_Header_t *out_hdr)
{
    OTA_Header_t hdr;
    static uint8_t hdr_buf[OTA_HDR_SIZE];
    static uint8_t sig_buf[OTA_SIG_MAX];
    static uint8_t io_buf[UPDATA_BUFF];
    static uint8_t digest[32];
    static uint8_t pubkey_buf[OTA_PUBKEY_LEN];
    mbedtls_sha256_context sha_ctx;
    mbedtls_pk_context pk;
    uint32_t remain;
    uint32_t addr;
    int ret = 0;

    Boot_ReadW25Q64Bytes(OTA_HDR_ADDR, hdr_buf, OTA_HDR_SIZE);
    if(!Boot_ParseOtaHeader(hdr_buf, OTA_HDR_SIZE, &hdr))
        return false;

    if(hdr.sig_len == 0U || hdr.sig_len > OTA_SIG_MAX)
        return false;

    Boot_ReadW25Q64Bytes(OTA_SIG_ADDR, sig_buf, hdr.sig_len);

    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    mbedtls_sha256_update(&sha_ctx, hdr_buf, OTA_HDR_SIZE);

    Boot_ReadW25Q64Bytes(OTA_META_ADDR, io_buf, OTA_META_LEN);
    mbedtls_sha256_update(&sha_ctx, io_buf, OTA_META_LEN);

    addr = OTA_STAGING_ADDR;
    remain = hdr.fw_size;
    while(remain > 0U)
    {
        uint32_t chunk = (remain > UPDATA_BUFF) ? UPDATA_BUFF : remain;
        Boot_ReadW25Q64Bytes(addr, io_buf, chunk);
        mbedtls_sha256_update(&sha_ctx, io_buf, chunk);
        addr += chunk;
        remain -= chunk;
    }

    mbedtls_sha256_finish(&sha_ctx, digest);
    mbedtls_sha256_free(&sha_ctx);

    Boot_ReadW25Q64Bytes(OTA_PUBKEY_ADDR, pubkey_buf, OTA_PUBKEY_LEN);
    mbedtls_pk_init(&pk);
    ret = mbedtls_pk_parse_public_key(&pk, pubkey_buf, OTA_PUBKEY_LEN);
    if(ret != 0)
    {
        mbedtls_pk_free(&pk);
        mbedtls_platform_zeroize(sig_buf, sizeof(sig_buf));
        mbedtls_platform_zeroize(io_buf, sizeof(io_buf));
        mbedtls_platform_zeroize(digest, sizeof(digest));
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig_buf, hdr.sig_len);
    mbedtls_pk_free(&pk);

    if(out_hdr != NULL)
        *out_hdr = hdr;

    mbedtls_platform_zeroize(sig_buf, sizeof(sig_buf));
    mbedtls_platform_zeroize(io_buf, sizeof(io_buf));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return (ret == 0);
}
