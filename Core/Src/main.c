/* USER CODE BEGIN Header */

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
OTA_Info_t OTA_Info;
UpData_A_t UpData_A;
OTA_State_t OTA_state;
// OTA_Version_t OTA_Version;
// uint8_t data_to_write[1024];
// uint8_t data_to_read[1024];
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_I2C1_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  Uart_Init(uart_rb);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // OTA_Info.OTA_Flag = 0x12121122;
  // for(uint8_t i = 0; i < 7; i++)
  // {
  //     OTA_Info.FileSize[i] = i;
  // }
  // AT24C64_WriteOTAInfo();
  // uint16_t num = Xmodem_CRC16(data, 5);
  // LOG_D("CRC: %x\r\n", num);
  AT24C64_ReadOTAInfo();
  
  // Uart_Printf("%X\r\n", OTA_Info.OTA_Flag);
  // for(uint8_t i = 0; i < 7; i++)
  // {
  //     Uart_Printf("%X\r\n", OTA_Info.FileSize[i]);
  // }
  BootLoader_Brance();
  // for(uint32_t i = 0; i < 64; i++)
  // {
  //     data_to_write[i] = 0x12; // 初始化数据
  // }
  // AT24C64_Write(0x0000, data_to_write, 64);
  // AT24C64_ReadPage(0x0000, data_to_read, 64);
  // for(uint32_t i = 0; i < 64; i++)
  // {
  //     Uart_Printf("%X\r\n", data_to_read[i]);
  // }
  // W25Q64_EraseBlock(0); // 擦除第0块

  // for(uint32_t i = 0; i < 1024; i++)
  // {
  //     data_to_write[i] = 0x12; // 初始化数据
  // }
  // W25Q64_PageProgram(0, data_to_write, 256); // 写入第0页
  // W25Q64_PageProgram(1, data_to_write, 256); // 写入第0页
  // W25Q64_PageProgram(2, data_to_write, 256); // 写入第0页
  // W25Q64_PageProgram(3, data_to_write, 256); // 写入第0页
  // W25Q64_ReadData(0, data_to_read, 1024); // 读取第0页
  // for(uint32_t i = 0; i < 1024; i++)
  // {
  //     Uart_Printf("%X\r\n", data_to_read[i]);
  // }
  // MCU_EraseFlash(10, 1); // 擦除从扇区10开始的2个扇区
  
  // for(uint32_t i = 0; i < (1024); i++)
  // {
  //     data_to_write[i] = 0x12345678; // 初始化数据
  // }
  // MCU_WriteFlash(0x08000000 + (1024*896), data_to_write, 1024); // 写入数据
  
  // MCU_ReadFlash(0x08000000 + (1024*896), data_to_read, 1024); // 读取数据
  
  // for(uint32_t i = 0; i < 1024; i++)
  // {
  //     Uart_Printf("%X\r\n", data_to_read[i]);
  // }
  
  while (1)
  {
    BootLoader_State();
    
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
