#ifndef __YMODEM_H__
#define __YMODEM_H__

#include <stdint.h>
#include <stdbool.h>

uint16_t Ymodem_CRC16(uint8_t *pdata, uint16_t length);
bool Ymodem_CheckPacket(const uint8_t *packet, uint16_t size, uint16_t *data_len, uint8_t *block_num);
bool Ymodem_ParseHeader(const uint8_t *data, uint16_t data_len, uint32_t *file_size, bool *is_end);
void Ymodem_WriteBlock(uint32_t block_index, const uint8_t *buf, uint32_t size, bool Where_to_store);
void Ymodem_Finalize(bool Where_to_store, uint32_t g_firmware_size);

#endif // __YMODEM_H__
