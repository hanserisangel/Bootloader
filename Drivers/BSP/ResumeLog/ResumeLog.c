#include "ResumeLog.h"
#include "W25Q64.h"
#include "main.h"

#include <stddef.h>
#include <string.h>

#define RESUME_LOG_MAGIC          0xA5A5A5A5U
#define RESUME_LOG_COMMIT         0x5A5A5A5AU
#define RESUME_LOG_TYPE_PROGRESS  0x0001U

#define RESUME_LOG_START_ADDR     OTA_HEAT_ADDR
#define RESUME_LOG_SIZE           (W25Q64_BLOCK_SIZE - OTA_HEAT_ADDR)
#define RESUME_LOG_END_ADDR       (RESUME_LOG_START_ADDR + RESUME_LOG_SIZE)

// 日志记录结构体，包含魔数、类型、长度、序列号、值、CRC 校验和、提交标志和保留字段
#pragma pack(push, 1)   // 确保结构体按1字节对齐，避免编译器添加填充字节
typedef struct
{
    uint32_t magic;     // 固定值 RESUME_LOG_MAGIC，用于识别有效记录
    uint16_t type;      // 记录类型
    uint16_t len;       // 记录长度
    uint32_t seq;       // 序列号
    uint32_t value;     // 值
    uint32_t crc;       // 校验和
    uint32_t commit;    // 提交标志，RESUME_LOG_COMMIT 表示记录已提交，0xFFFFFFFFU 表示未提交
    uint32_t reserved;  // 保留字段
} ResumeLog_Record_t;
#pragma pack(pop)   // 恢复默认对齐方式

// 全局状态变量，跟踪日志的写入地址、最新有效记录的信息和初始化状态
typedef struct
{
    uint32_t write_addr;        // 下一条记录的写入地址
    uint32_t latest_addr;       // 最新有效记录的地址

    uint32_t latest_seq;        // 最新有效记录的序列号
    uint32_t latest_value;      // 最新有效记录的值

    bool latest_valid;          // 最新有效记录是否存在
    bool initialized;           // 是否已经初始化
} ResumeLog_State_t;

static ResumeLog_State_t g_state = {0};

static bool ResumeLog_WriteRecord(uint32_t value, uint32_t seq);

static uint32_t ResumeLog_Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (uint32_t j = 0; j < 8; ++j)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

/**
 * @brief  计算记录的 CRC32 校验值
 * @note    计算时不包含 crc 字段本身，确保记录内容的完整性验证
 * @param  rec: 指向要计算 CRC 的记录结构体的指针
 * @retval 计算得到的 CRC32 校验值
 */
static uint32_t ResumeLog_CalcRecordCrc(const ResumeLog_Record_t *rec)
{
    return ResumeLog_Crc32((const uint8_t *)rec, (uint32_t)offsetof(ResumeLog_Record_t, crc));
}   // offsetof(ResumeLog_Record_t, crc)，确保计算 CRC 时不包含 crc 字段本身

/**
 * @brief  检查数据是否全为 0xFF，判断是否为擦除状态
 * @param  data: 指向要检查的数据的指针
 * @param  len: 数据的长度（以字节为单位）
 * @retval true 数据全为 0xFF，表示擦除状态；false 数据中存在非 0xFF 的字节，表示不是擦除状态
 */
static bool ResumeLog_IsBlank(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        if (data[i] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief  检查记录的有效性，验证记录的魔数、类型、长度和 CRC 是否符合预期
 * @param  rec: 指向要验证的记录结构体的指针
 * @retval true 记录有效，符合预期格式和 CRC 校验；false 记录无效，可能是擦除状态或数据损坏
 */
static bool ResumeLog_IsValidRecord(const ResumeLog_Record_t *rec)
{
    if (rec->magic != RESUME_LOG_MAGIC)
    {
        return false;
    }
    if (rec->commit != RESUME_LOG_COMMIT)
    {
        return false;
    }
    if (rec->type != RESUME_LOG_TYPE_PROGRESS)
    {
        return false;
    }
    if (rec->len != sizeof(uint32_t))
    {
        return false;
    }
    return (rec->crc == ResumeLog_CalcRecordCrc(rec));
}

/**
 * @brief  扫描日志区域，找到最新有效的记录并更新全局状态
 * @note    更新全局状态
 * @retval None
 */
static void ResumeLog_Scan(void)
{
    uint32_t first_blank = RESUME_LOG_END_ADDR;

    g_state.latest_valid = false;
    g_state.latest_seq = 0;
    g_state.latest_value = 0;
    g_state.latest_addr = 0xFFFFFFFFU;
    
    // 遍历日志区域，检查每条记录的有效性，并找到最新的有效记录
    for (uint32_t addr = RESUME_LOG_START_ADDR;
         addr + sizeof(ResumeLog_Record_t) <= RESUME_LOG_END_ADDR;
         addr += sizeof(ResumeLog_Record_t))
    {
        ResumeLog_Record_t rec;
        W25Q64_ReadBytes(addr, (uint8_t *)&rec, sizeof(rec));

        // 如果记录全为 0xFF，表示该区域未写入过数据，记录第一个空白地址以便后续写入使用
        if (ResumeLog_IsBlank((const uint8_t *)&rec, sizeof(rec)))
        {
            if (first_blank == RESUME_LOG_END_ADDR)
            {
                first_blank = addr;
            }
            continue;
        }

        // 检查记录的有效性，验证魔数、类型、长度和 CRC 是否符合预期
        if (ResumeLog_IsValidRecord(&rec))
        {
            // 如果记录有效，并且序列号比当前最新的还大，更新全局状态为最新记录的信息
            if (!g_state.latest_valid || rec.seq > g_state.latest_seq)
            {
                g_state.latest_valid = true;
                g_state.latest_seq = rec.seq;
                g_state.latest_value = rec.value;
                g_state.latest_addr = addr;
            }
        }
    }

    // 设置下一条记录的写入地址，如果找到空白地址则使用空白地址，否则从日志区域末尾开始覆盖旧记录
    if (first_blank != RESUME_LOG_END_ADDR)
    {
        g_state.write_addr = first_blank;
    }
    else
    {
        g_state.write_addr = RESUME_LOG_END_ADDR;
    }
}

/**
 * @brief  擦除日志区域，清除所有记录
 * @note    通过擦除日志区域的所有扇区，清除所有记录，并重置全局状态以反映没有有效记录的状态
 * @retval None
 */
static void ResumeLog_EraseRegion(void)
{
    for (uint32_t addr = RESUME_LOG_START_ADDR; addr < RESUME_LOG_END_ADDR; addr += W25Q64_SECTOR_SIZE)
    {
        uint32_t sector = addr / W25Q64_SECTOR_SIZE;
        W25Q64_EraseSector(sector);
    }
}

/**
 * @brief  回收日志区域，擦除所有记录并保留最新有效记录（如果存在）
 * @note    在日志区域即将写满时调用，擦除所有记录以释放空间，
 *          但如果存在最新有效记录，则在擦除后重新写入该记录，以确保重要的进度信息不会丢失
 * @retval None
 */
static void ResumeLog_RecycleAll(void)
{
    bool keep_latest = g_state.latest_valid;
    uint32_t keep_value = g_state.latest_value;
    uint32_t keep_seq = g_state.latest_seq;

    ResumeLog_EraseRegion();

    g_state.latest_valid = false;
    g_state.latest_seq = 0;
    g_state.latest_value = 0;
    g_state.latest_addr = 0xFFFFFFFFU;
    g_state.write_addr = RESUME_LOG_START_ADDR;

    if (keep_latest)
    {
        (void)ResumeLog_WriteRecord(keep_value, keep_seq + 1U);
    }
}

/**
 * @brief  写入一条记录到日志区域
 * @note    将给定的值和序列号封装成一条记录，计算 CRC 校验和，并写入到日志区域的当前写入地址
 * @retval true 记录写入成功；false 记录写入失败
 */
static bool ResumeLog_WriteRecord(uint32_t value, uint32_t seq)
{
    ResumeLog_Record_t rec;

    memset(&rec, 0xFF, sizeof(rec));
    rec.magic = RESUME_LOG_MAGIC;
    rec.type = RESUME_LOG_TYPE_PROGRESS;
    rec.len = sizeof(uint32_t);
    rec.seq = seq;
    rec.value = value;
    rec.crc = ResumeLog_CalcRecordCrc(&rec);
    rec.commit = 0xFFFFFFFFU;       // 先写入未提交状态，等数据写入完成后再更新为已提交状态，确保记录的原子性
    rec.reserved = 0xFFFFFFFFU;

    W25Q64_WriteBytes(g_state.write_addr, (const uint8_t *)&rec, sizeof(rec));

    {
        uint32_t commit = RESUME_LOG_COMMIT;
        W25Q64_WriteBytes(g_state.write_addr + offsetof(ResumeLog_Record_t, commit),
                          (const uint8_t *)&commit,
                          sizeof(commit));
    }

    // 更新全局状态为最新记录的信息，以便后续读取和写入操作使用
    g_state.latest_valid = true;
    g_state.latest_seq = seq;
    g_state.latest_value = value;
    g_state.latest_addr = g_state.write_addr;
    g_state.write_addr += sizeof(ResumeLog_Record_t);

    return true;
}

/**
 * @brief  日志系统初始化函数，扫描日志区域以找到最新有效的记录，并更新全局状态（掉电恢复）
 * @retval None
 */
void ResumeLog_Init(void)
{
    ResumeLog_Scan();
    g_state.initialized = true;
}

/**
 * @brief  读取最新有效记录的值和序列号
 * @note    如果存在有效记录，则通过输出参数返回最新记录的值和序列号
 * @param  value: 输出参数，指向存储最新记录值的变量的指针
 * @param  seq: 输出参数，指向存储最新记录序列号的变量的指针
 * @retval true 成功读取到有效记录；false 没有有效记录可读
 */
bool ResumeLog_ReadLatest(uint32_t *value, uint32_t *seq)
{
    if (!g_state.initialized)
    {
        ResumeLog_Init();
    }

    if (!g_state.latest_valid)
    {
        return false;
    }

    if (value != NULL)
    {
        *value = g_state.latest_value;
    }
    if (seq != NULL)
    {
        *seq = g_state.latest_seq;
    }
    return true;
}

/**
 * @brief  写入进度记录到日志区域（主要接口）
 * @note    将给定的进度值写入到日志区域
 * @param  value: 要写入的进度值
 * @retval true 写入成功；false 写入失败
 */
bool ResumeLog_WriteProgress(uint32_t value)
{
    uint32_t next_seq = 0;

    if (!g_state.initialized)
    {
        ResumeLog_Init();
    }

    if (g_state.latest_valid)
    {
        next_seq = g_state.latest_seq + 1U;
    }

    // 如果当前写入地址已经接近日志区域的末尾，无法容纳下一条记录，则回收日志区域，擦除所有记录以释放空间，
    // 但如果存在最新有效记录，则在擦除后重新写入该记录，以确保重要的进度信息不会丢失
    if (g_state.write_addr + sizeof(ResumeLog_Record_t) > RESUME_LOG_END_ADDR)
    {
        ResumeLog_RecycleAll();
        next_seq = g_state.latest_valid ? (g_state.latest_seq + 1U) : 0U;
    }

    return ResumeLog_WriteRecord(value, next_seq);
}

/**
 * @brief  擦除日志区域，清除所有记录
 * @note    通过擦除日志区域的所有扇区，清除所有记录，并重置全局状态以反映没有有效记录的状态
 * @retval None
 */
void ResumeLog_EraseAll(void)
{
    ResumeLog_EraseRegion();

    g_state.latest_valid = false;
    g_state.latest_seq = 0;
    g_state.latest_value = 0;
    g_state.latest_addr = 0xFFFFFFFFU;
    g_state.write_addr = RESUME_LOG_START_ADDR;
    g_state.initialized = true;
}
