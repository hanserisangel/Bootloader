#include "BootDelta.h"
#include <string.h>
#include "main.h"
#include "BootOta.h"
#include "BootCrypto.h"
#include "hpatch_lite.h"
#include "tuz_dec.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform_util.h"

#define DELTA_WRITE_BUF 1024U               // 增量更新过程中写入 Flash 的缓冲区大小
#define DELTA_PATCH_CACHE 1024U             // HPatchLite 处理增量包时的缓存区大小，必须足够大以容纳 HPatchLite 内部的处理需求
#define DELTA_TUZ_DICT_MAX OTA_TUZ_DICT_MAX // 增量更新过程中 TUZ 解压的字典最大大小，必须足够大以容纳 HPatchLite 内部的处理需求
#define DELTA_TUZ_CACHE OTA_TUZ_CACHE_SIZE  // 增量更新过程中 TUZ 解压的缓存区大小，必须足够大以容纳 HPatchLite 内部的处理需求

static uint8_t g_tuz_mem[DELTA_TUZ_DICT_MAX + DELTA_TUZ_CACHE]; // 增量更新过程中 TUZ 解压的字典和缓存区

// static void Delta_PrintBytes16(const char *tag, const uint8_t *buf, uint32_t len)
// {
//     uint32_t n = (len < 16U) ? len : 16U;
//     Uart_Printf("%s", tag);
//     for(uint32_t i = 0; i < n; i++)
//     {
//         Uart_Printf(" %02X", buf[i]);
//     }
//     Uart_Printf("\r\n");
// }

static void Delta_PrintFlash16(const char *tag, uint32_t addr)
{
    Delta_PrintBytes16(tag, (const uint8_t *)addr, 16U);
}

typedef struct {
    hpatchi_listener_t listener;    // HPatchLite 监听器，提供读取差分数据、读取旧数据和写入新数据的接口
    uint32_t diff_addr;         // W25Q64 中差分包的起始地址
    uint32_t diff_size;         // 差分包大小（字节）
    uint32_t diff_cursor;       // 当前在差分包中的读取位置（字节偏移）
    uint32_t base_addr;         // 旧版本固件（基准）在 MCU Flash 中的起始地址
    uint32_t target_addr;       // 目标地址，增量更新后的新版本固件将被写入这个地址
    uint32_t out_written;       // 已经写入目标地址的字节数
    uint8_t write_buf[DELTA_WRITE_BUF + 4U];
    uint32_t write_buf_len;
    bool aes_ready;
    mbedtls_aes_context aes;
    size_t nc_off;                      // AES-CTR 模式的偏移量，表示当前流块中已经使用的字节数（0-15）
    uint8_t stream_block[16];           // AES-CTR 模式的流块缓冲区
    uint8_t nonce_counter[16];
    uint8_t cipher_buf[UPDATA_BUFF];    // 存储从 W25Q64 读取的差分包数据
    tuz_TStream tuz_stream;
    // bool debug_first_write_dumped;
} BootDeltaCtx;

/**
 * @brief  刷新写入缓冲区
 * @param  ctx: BootDeltaCtx 指针
 * @param  is_final: 是否为最终写入
 * @retval true 成功，false 失败
 */
static bool Delta_FlushWrite(BootDeltaCtx *ctx, bool is_final)
{
    uint32_t flush_len;

    if(ctx->write_buf_len == 0U)
        return true;

    if(is_final)
    {
        flush_len = (ctx->write_buf_len + 3U) & ~3U;
        if(flush_len > sizeof(ctx->write_buf))
            return false;
        if(flush_len > ctx->write_buf_len)
            memset(ctx->write_buf + ctx->write_buf_len, 0xFF, flush_len - ctx->write_buf_len);
    }
    else
    {
        flush_len = ctx->write_buf_len & ~3U;
        if(flush_len == 0U)
            return true;
    }
    // if(!ctx->debug_first_write_dumped)
    // {
    //     Uart_Printf("Delta first flush addr=0x%08lX len=%lu\r\n",
    //         (unsigned long)(ctx->target_addr + ctx->out_written),
    //         (unsigned long)flush_len);
    //     Delta_PrintBytes16("Delta first flush data:", ctx->write_buf, flush_len);
    //     ctx->debug_first_write_dumped = true;
    // }

    MCU_WriteFlash(ctx->target_addr + ctx->out_written, (uint32_t *)ctx->write_buf, flush_len / 4U);
    ctx->out_written += flush_len;

    if(!is_final)
    {
        uint32_t remain = ctx->write_buf_len - flush_len;
        if(remain > 0U)
            memmove(ctx->write_buf, ctx->write_buf + flush_len, remain);
        ctx->write_buf_len = remain;
    }
    else
    {
        ctx->write_buf_len = 0U;
    }

    return true;
}

/**
 * @brief  从 W25Q64 中读取 HPatchLite 增量包密文并在线解密
 * @param  inputStream: HPatchLite 输入流句柄，实际上是指向 BootDeltaCtx 的指针
 * @param  out_data: 输出参数，指向存储读取数据的缓冲区
 * @param  data_size: 输入参数，指定要读取的数据大小；输出参数，实际读取的数据大小
 * @retval true 成功，false 失败
 */
static hpi_BOOL Delta_ReadDiff(hpi_TInputStreamHandle inputStream, hpi_byte *out_data, hpi_size_t *data_size)
{
    BootDeltaCtx *ctx = (BootDeltaCtx *)inputStream;
    hpi_size_t remain;
    hpi_size_t need;
    hpi_size_t out_pos = 0;

    if(data_size == NULL || out_data == NULL)
        return hpi_FALSE;

    if(!ctx->aes_ready)
        return hpi_FALSE;

    if(ctx->diff_cursor >= ctx->diff_size)
    {
        *data_size = 0;
        return hpi_TRUE;
    }

    remain = (hpi_size_t)(ctx->diff_size - ctx->diff_cursor);
    need = *data_size;
    if(need > remain)
        need = remain;

    while(out_pos < need)
    {
        uint32_t chunk = (uint32_t)(need - out_pos);
        if(chunk > UPDATA_BUFF)
            chunk = UPDATA_BUFF;

        Boot_ReadW25Q64Bytes(ctx->diff_addr + ctx->diff_cursor, ctx->cipher_buf, chunk);
        if(mbedtls_aes_crypt_ctr(&ctx->aes, chunk, &ctx->nc_off,
            ctx->nonce_counter, ctx->stream_block,
            ctx->cipher_buf, out_data + out_pos) != 0)
        {
            return hpi_FALSE;
        }

        ctx->diff_cursor += chunk;
        out_pos += chunk;
    }

    *data_size = out_pos;
    return hpi_TRUE;
}

static tuz_BOOL Delta_ReadDiffTuzRaw(tuz_TInputStreamHandle inputStream, tuz_byte *out_data, tuz_size_t *data_size)
{
    hpi_size_t sz = (hpi_size_t)(*data_size);
    if(!Delta_ReadDiff((hpi_TInputStreamHandle)inputStream, (hpi_byte *)out_data, &sz))
        return tuz_FALSE;
    *data_size = (tuz_size_t)sz;
    return tuz_TRUE;
}

static hpi_BOOL Delta_ReadDiffTuz(hpi_TInputStreamHandle inputStream, hpi_byte *out_data, hpi_size_t *data_size)
{
    BootDeltaCtx *ctx = (BootDeltaCtx *)inputStream;
    hpi_size_t requested;
    hpi_size_t produced = 0;

    if(data_size == NULL || out_data == NULL)
        return hpi_FALSE;

    requested = *data_size;
    while(produced < requested)
    {
        tuz_size_t part = (tuz_size_t)(requested - produced);
        tuz_TResult ret = tuz_TStream_decompress_partial(&ctx->tuz_stream, out_data + produced, &part);
        produced += (hpi_size_t)part;

        if(ret == tuz_OK)
        {
            if(part == 0U)
                return hpi_FALSE;
            continue;
        }
        if(ret == tuz_STREAM_END)
        {
            *data_size = produced;
            return hpi_TRUE;
        }
        return hpi_FALSE;
    }

    *data_size = produced;
    return hpi_TRUE;
}

/**
 * @brief  从 MCU Flash 读取基准固件数据
 * @param  listener: HPatchLite 监听器
 * @param  read_from_pos: 读取位置，相对于基准固件起始地址的字节偏移
 * @param  out_data: 输出参数，指向存储读取数据的缓冲区
 * @param  data_size: 输入参数，指定要读取的数据大小；输出参数，实际读取的数据大小
 * @retval true 成功，false 失败
 */
static hpi_BOOL Delta_ReadOld(hpatchi_listener_t *listener, hpi_pos_t read_from_pos, hpi_byte *out_data, hpi_size_t data_size)
{
    BootDeltaCtx *ctx = (BootDeltaCtx *)listener;

    memcpy(out_data, (const void *)(ctx->base_addr + read_from_pos), data_size);
    return hpi_TRUE;
}

/**
 * @brief  将增量更新后的新版本固件数据写入 MCU Flash
 * @param  listener: HPatchLite 监听器，实际上是指向 BootDeltaCtx 的指针
 * @param  data: 指向要写入数据的指针
 * @param  data_size: 要写入的数据大小（字节）
 * @retval true 成功，false 失败
 */
static hpi_BOOL Delta_WriteNew(hpatchi_listener_t *listener, const hpi_byte *data, hpi_size_t data_size)
{
    BootDeltaCtx *ctx = (BootDeltaCtx *)listener;
    hpi_size_t copied = 0;

    while(copied < data_size)
    {
        hpi_size_t room = (hpi_size_t)(DELTA_WRITE_BUF - ctx->write_buf_len);
        hpi_size_t n = data_size - copied;
        if(n > room)
            n = room;

        memcpy(ctx->write_buf + ctx->write_buf_len, data + copied, n);
        ctx->write_buf_len += (uint32_t)n;
        copied += n;

        if(ctx->write_buf_len >= DELTA_WRITE_BUF)
        {
            if(!Delta_FlushWrite(ctx, false))
                return hpi_FALSE;
        }
    }

    return hpi_TRUE;
}

/**
 * @brief  从 W25Q64 中读取 HPatchLite 增量包明文，应用到目标 Flash 区域。
 * @param  delta_addr: W25Q64 中差分包起始地址
 * @param  delta_size: 差分包大小（字节）
 * @param  base_addr: 旧版本固件（基准）起始地址
 * @param  target_addr: 新版本固件（写入目标）起始地址
 * @retval true 成功，false 失败
 */
bool Boot_ApplyDeltaFromW25Q64(uint32_t delta_addr, uint32_t delta_size, uint32_t base_addr, uint32_t target_addr)
{
    BootDeltaCtx ctx;
    hpi_compressType compress_type;
    hpi_pos_t new_size = 0;         // 增量更新后新版本固件的大小
    hpi_pos_t uncompress_size = 0;  // 增量包解压后的大小（字节）
    static uint8_t temp_cache[DELTA_PATCH_CACHE];   // HPatchLite 增量更新过程中的临时缓存区
    uint8_t key[16];                // AES-128 密钥，派生自 OTA 元数据
    uint8_t meta[OTA_META_LEN];
    const uint8_t *iv;
    bool ok = false;

    if(delta_size == 0U)
        return false;

    memset(&ctx, 0, sizeof(ctx));
    ctx.diff_addr = delta_addr;
    ctx.diff_size = delta_size;
    ctx.base_addr = base_addr;
    ctx.target_addr = target_addr;

    // Uart_Printf("Delta begin diff=0x%08lX size=%lu base=0x%08lX target=0x%08lX\r\n",
    //     (unsigned long)delta_addr,
    //     (unsigned long)delta_size,
    //     (unsigned long)base_addr,
    //     (unsigned long)target_addr);
    // Delta_PrintFlash16("Delta base[0:16]:", base_addr);

    // 1. 从 W25Q64 读取元数据并派生 AES 密钥，准备 AES-CTR 模式解密增量包数据
    Boot_ReadW25Q64Bytes(OTA_META_ADDR, meta, OTA_META_LEN);
    if(!Boot_DeriveAesKey(key, sizeof(key), meta))
        return false;

    mbedtls_aes_init(&ctx.aes);
    if(mbedtls_aes_setkey_enc(&ctx.aes, key, 128) != 0)
        goto cleanup;

    iv = meta + OTA_ECDH_PUB_LEN + OTA_SALT_LEN;
    memcpy(ctx.nonce_counter, iv, sizeof(ctx.nonce_counter));
    memset(ctx.stream_block, 0, sizeof(ctx.stream_block));
    ctx.nc_off = 0;
    ctx.aes_ready = true;

    // 2. 初始化 HPatchLite 监听器，提供读取差分数据、读取旧数据和写入新数据的接口
    ctx.listener.diff_data = &ctx;
    ctx.listener.read_diff = Delta_ReadDiff;    // 提供读取差分数据的接口，从 W25Q64 中读取增量包数据
    ctx.listener.read_old = Delta_ReadOld;      // 提供读取旧数据的接口，从 MCU Flash 中读取基准固件数据
    ctx.listener.write_new = Delta_WriteNew;    // 提供写入新数据的接口，将增量更新后的新版本固件数据写入 MCU Flash

    // 3. 打开 HPatchLite 增量包，HPatchLite 会通过 listener 接口读取差分数据、读取旧数据和写入新数据
    if(!hpatch_lite_open(ctx.listener.diff_data, ctx.listener.read_diff, &compress_type, &new_size, &uncompress_size))
    {
        LOG_E("hpatch_lite_open failed");
        return false;
    }

    // 4. 根据 HPatchLite 返回的压缩类型，初始化 TUZ 解压器（如果使用了 TUZ 压缩）
    if(compress_type == hpi_compressType_no)
    {
        if(uncompress_size != 0)
        {
            LOG_E("HPatchLite uncompress size invalid");
            return false;
        }
    }
    else if(compress_type == hpi_compressType_tuz)
    {
        // TUZ 解压需要先读取字典大小，然后打开 TUZ 解压器
        tuz_size_t dict_size = tuz_TStream_read_dict_size((tuz_TInputStreamHandle)&ctx, Delta_ReadDiffTuzRaw);
        if((dict_size < tuz_kMinOfDictSize) || (dict_size > DELTA_TUZ_DICT_MAX))
        {
            LOG_E("tinyuz dict size invalid");
            return false;
        }

        // 打开 TUZ 解压器，提供读取差分数据的接口，使用预先分配的内存作为字典和缓存区
        if(tuz_TStream_open(&ctx.tuz_stream,
            (tuz_TInputStreamHandle)&ctx,
            Delta_ReadDiffTuzRaw,
            g_tuz_mem,
            dict_size,
            DELTA_TUZ_CACHE) != tuz_OK)
        {
            LOG_E("tinyuz open failed");
            return false;
        }

        ctx.listener.read_diff = Delta_ReadDiffTuz;
    }
    else
    {
        LOG_E("HPatchLite compress type unsupported");
        return false;
    }

    // 5. 应用增量包，HPatchLite 会通过 listener 接口读取差分数据、读取旧数据和写入新数据
    if(!hpatch_lite_patch(&ctx.listener, new_size, temp_cache, (hpi_size_t)sizeof(temp_cache)))
    {
        LOG_E("hpatch_lite_patch failed");
        goto cleanup;
    }

    // 6. 刷新剩余的待写数据到 Flash
    if(!Delta_FlushWrite(&ctx, true))
        goto cleanup;

    ok = (ctx.out_written == (uint32_t)new_size);

    // LOG_I("Delta patch applied, new size: %lu, written: %lu", new_size, ctx.out_written);
    // Delta_PrintFlash16("Delta target[0:16]:", target_addr);

cleanup:
    mbedtls_aes_free(&ctx.aes);
    mbedtls_platform_zeroize(key, sizeof(key));
    return ok;
}
