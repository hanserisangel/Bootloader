#include "usart.h"
#include "Uart.h"
#include "main.h"

static struct ringbuffer _uart_rb = {0}; // 环形缓冲区实例
ringbuffer_t uart_rb = &_uart_rb;
uint8_t dma_buff[RX_DATA_CELLING];
uint8_t uart_buff[RX_BUF_SIZE];

void Uart_Init(ringbuffer_t rb)
{
	// MX_USART3_UART_Init();
	// MX_USART1_UART_Init();
	// uint8_t data = 0;
	
	rb->r = 0; // 块指针指向块数组的第一个成员
	rb->w = 0;
	rb->TypeCount = 0;
	rb->BlockCount = 0;
	memset(uart_buff, 0, sizeof(uart_buff));
	memset(rb->buffer, 0, sizeof(rb->buffer));

	HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buff, RX_DATA_CELLING); // Start UART reception in DMA mode
}

void Uart_Printf(const char *fmt, ...)
{
	char buffer[RX_BUF_SIZE];
	
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	HAL_UART_Transmit(&huart3, (uint8_t*)buffer, strlen(buffer), HAL_MAX_DELAY);
}

/**
  * @brief  环形缓冲区（ringbuffer）：先入先出fifo
  * @note   在环形缓冲区uart_buff还有充足余量的条件下进行的处理
  * @param  rb: 环形缓冲区指针
  * @param  data: 要存入的数据
  * @retval None
**/
void RingBuffer_Put(ringbuffer_t rb, uint8_t* data, uint16_t length)
{
	if ((rb->BlockCount < RX_BLOCK_NUM)) // 检查块缓冲区是否已满
	{
		rb->buffer[rb->w].start = rb->TypeCount;
		rb->buffer[rb->w].end = rb->TypeCount + length; // end 不包含在块里

		rb->TypeCount += length;

		// 拷贝数据
		memcpy(uart_buff + rb->buffer[rb->w].start, data, sizeof(uint8_t)*length);

		// 更新写指针
		rb->w = (rb->w + 1) % RX_BLOCK_NUM;
		rb->BlockCount ++;
	}
	// else 缓冲区满，进行数据覆盖
	else
	{
		rb->buffer[rb->w].start = rb->TypeCount;
		rb->buffer[rb->w].end = rb->TypeCount + length; // end 不包含在块里

		rb->TypeCount += length;
		// 拷贝数据
		memcpy(uart_buff + rb->buffer[rb->w].start, data, sizeof(uint8_t)*length);

		rb->w = (rb->w + 1) % RX_BLOCK_NUM;
		rb->r = (rb->r + 1) % RX_BLOCK_NUM;
	}
}

uint16_t RingBuffer_Get(ringbuffer_t rb, uint8_t* data)
{
	uint16_t length = 0;
	if (rb->BlockCount > 0) // 检查缓冲区是否为空
	{
		length = rb->buffer[rb->r].end - rb->buffer[rb->r].start;
		memcpy(data, uart_buff + rb->buffer[rb->r].start, sizeof(uint8_t)*length);
		
		rb->r = (rb->r + 1) % RX_BLOCK_NUM; // 更新读指针
		rb->BlockCount --; // 减少数据计数
	}
	return length;
}

void RingBuffer_Clear(ringbuffer_t rb)
{
	rb->r = 0;
	rb->w = 0;
	rb->BlockCount = 0;
	rb->TypeCount = 0;
	memset(rb->buffer, 0, sizeof(rb->buffer));
	memset(uart_buff, 0, sizeof(uart_buff));
}

// 使用 UART 扩展接收事件回调处理 IDLE 事件
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart == &huart3)
  {
	// 复制接收到的数据到环形缓冲区
	if((RX_BUF_SIZE - uart_rb->TypeCount < RX_DATA_CELLING))
	{
		uart_rb->TypeCount = 0;
	}
	RingBuffer_Put(uart_rb, dma_buff, Size);

	// 重新启动 DMA 接收
	HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buff, RX_DATA_CELLING);
  } 
}
