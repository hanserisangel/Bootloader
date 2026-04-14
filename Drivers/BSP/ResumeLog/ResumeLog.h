#ifndef __RESUME_LOG_H__
#define __RESUME_LOG_H__

#include <stdint.h>
#include <stdbool.h>

void ResumeLog_Init(void);
bool ResumeLog_ReadLatest(uint32_t *value, uint32_t *seq);
bool ResumeLog_WriteProgress(uint32_t value);
void ResumeLog_EraseAll(void);

#endif // __RESUME_LOG_H__
