/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "headers.h"

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
#if defined ( __ICCARM__ ) /*!< IAR Compiler */
#pragma location=0x30000000
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x30000080
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x30000000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x30000080))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */

ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((section(".RxDescripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((section(".TxDescripSection")));   /* Ethernet Tx DMA Descriptors */
#endif

ETH_TxPacketConfig TxConfig APP_DTCM_BSS;

ETH_HandleTypeDef heth APP_DTCM_BSS;

RNG_HandleTypeDef hrng;

TIM_HandleTypeDef htim6 APP_DTCM_BSS;

UART_HandleTypeDef huart3 APP_DTCM_BSS;
DMA_HandleTypeDef hdma_usart3_tx APP_DTCM_BSS;
DMA_HandleTypeDef hdma_usart3_rx APP_DTCM_BSS;

/* USER CODE BEGIN PV */
uint8_t RXbuffer[ETH_RX_DESC_CNT][APP_ETH_RX_BUFFER_SIZE] __attribute__((aligned(32))) __attribute__((section(".RxBufferSection")));
uint8_t TXbuffer[ETH_TX_DESC_CNT][APP_ETH_TX_BUFFER_SIZE] __attribute__((aligned(32))) __attribute__((section(".TxBufferSection")));
packetinf inf[ETH_RX_DESC_CNT] APP_DTCM_BSS;
tempips tempss;
uint32_t tim6time APP_DTCM_BSS = 0;
volatile uint32_t dwt_measurements[DWT_METRIC_COUNT][DWT_SAMPLE_CAPACITY] APP_DWT_BSS;
volatile uint16_t dwt_measurement_counts[DWT_METRIC_COUNT] APP_DWT_BSS;
volatile uint32_t dwt_measurement_dropped[DWT_METRIC_COUNT] APP_DWT_BSS;
volatile uint32_t dwt_packet_dropped APP_DWT_BSS;
_Static_assert(DWT_METRIC_COUNT == APP_DWT_CHANNELS, "DWT channel count mismatch");




const MAC_Addr_t MAC_BROADCAST = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_RNG_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	tempss = (tempips){
      .IPv4_add = { .addr = APP_DEVICE_IPV4_ADDRESS },

	};
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
  DWT_Init();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_RNG_Init();
  TCP_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  memset(DMARxDscrTab, 0, sizeof(DMARxDscrTab));

  for(int i = 0; i < ETH_RX_DESC_CNT; i++)
  {
      DMARxDscrTab[i].DESC0 = (uint32_t)&RXbuffer[i][0];
      DMARxDscrTab[i].DESC1 = 0;
      DMARxDscrTab[i].DESC2 = 0;
      DMARxDscrTab[i].DESC3 = 0xC1000000;
  }

  for(int i =0; i < ETH_TX_DESC_CNT; i++)
  {
      DMATxDscrTab[i].DESC0 = 0;
      DMATxDscrTab[i].DESC1 = 0;
      DMATxDscrTab[i].DESC2 = 0;
      DMATxDscrTab[i].DESC3 = 0;
  }

  uint8_t startup_ok = 1;
  ETH_MACFilterConfigTypeDef FilterConf = {0};
  FilterConf.PromiscuousMode = 1;
  FilterConf.BroadcastFilter = 0;
  FilterConf.PassAllMulticast = 1;
  if (HAL_ETH_SetMACFilterConfig(&heth, &FilterConf) != HAL_OK)
  {
      startup_ok = 0;
  }

  HAL_NVIC_SetPriority(ETH_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(ETH_IRQn);

  if (HAL_ETH_Start_IT(&heth) != HAL_OK)
  {
      startup_ok = 0;
  }
  else
  {
      uint32_t errorCode = HAL_ETH_GetError(&heth);
      uint32_t dmaError = HAL_ETH_GetDMAError(&heth);
      if (errorCode != 0 || dmaError != 0)
      {
        startup_ok = 0;
      }
  }

  initarp();

  HAL_Delay(APP_STARTUP_DELAY_MS);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)
    {
      startup_ok = 0;
    }
    if (startup_ok)
    {
      writeline("Startup complete: all peripherals initialized");
    }
  while (1)
  {
	  console_process();


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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

  static uint8_t MACAddr[6] = APP_DEVICE_MAC_ADDRESS;

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  heth.Init.MACAddr = MACAddr;
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = APP_ETH_RX_BUFFER_SIZE;


  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH_Init 2 */

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 23999;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 1999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */06.10. - 09

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();06.10. - 09
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void DWT_Init(void)
{
  for (unsigned int metric = 0; metric < DWT_METRIC_COUNT; metric++) {
    dwt_measurement_counts[metric] = 0;
    dwt_measurement_dropped[metric] = 0;
    for (unsigned int sample = 0; sample < DWT_SAMPLE_CAPACITY; sample++) {
      dwt_measurements[metric][sample] = 0;
    }
  }
  dwt_packet_dropped = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void DWT_Record(DwtMetric metric, uint32_t cycles)
{
  if ((unsigned int)metric >= DWT_METRIC_COUNT) {
    return;
  }

  uint16_t count = dwt_measurement_counts[metric];
  if (count < DWT_SAMPLE_CAPACITY) {
    dwt_measurements[metric][count] = cycles;
    dwt_measurement_counts[metric] = count + 1;
  } else {
    dwt_measurements[metric][DWT_SAMPLE_CAPACITY - 1] = cycles;
    dwt_measurement_dropped[metric]++;
  }
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth)
{
    uint32_t descidx = heth->RxDescList.RxDescIdx;
    ETH_DMADescTypeDef *dmarxdesc = &DMARxDscrTab[descidx];

    while ((dmarxdesc->DESC3 & 0x80000000) == 0)
    {


      uint32_t rx_status = dmarxdesc->DESC3;
      __DMB();
      uint16_t rx_length = rx_status & ETH_DMARXNDESCWBF_PL;
      ETHFrame *Frame = (ETHFrame *)&RXbuffer[descidx][0];






      inf[descidx].eth_pointer = Frame;
      inf[descidx].parsed_offset = sizeof(ETHFrame);
      inf[descidx].frame_length = rx_length;

      if ((rx_status & (ETH_DMARXNDESCWBF_FD | ETH_DMARXNDESCWBF_LD)) ==
          (ETH_DMARXNDESCWBF_FD | ETH_DMARXNDESCWBF_LD) &&
          rx_length >= sizeof(ETHFrame) && rx_length <= APP_ETH_RX_BUFFER_SIZE) {
        uint32_t dwt_start = DWT_START();
        switch(htons(Frame->ETHtype))
        {
          case 0x0806:
            if (rx_length >= sizeof(ETHFrame) + sizeof(arpheader)) {
              arp(&RXbuffer[descidx][sizeof(ETHFrame)]);
            }
            break;

          case 0x0800:
            packetHandler(descidx);
            break;

          case 0x88CC:
            break;

          default:
            break;
        }
        DWT_Record(DWT_METRIC_PACKET_TRANSACTION, DWT_STOP(dwt_start));
      }

		if(inf[descidx].bufflock == 0)
		{
    __DMB();
            dmarxdesc->DESC3 = 0xC1000000;
    __DMB();
    heth->Instance->DMACRDTPR = (uint32_t)dmarxdesc;
		}

        descidx = (descidx + 1) % ETH_RX_DESC_CNT;
        dmarxdesc = &DMARxDscrTab[descidx];
    }

    heth->RxDescList.RxDescIdx = descidx;
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM6)
    {
        tim6time += 1;
        arp_aging();
        mac_table_aging();
        topology_aging();
		TCP_retrans();
        TCP_clean_table();
    }
}

bufferinf tcp_buff_lock[ETH_TX_DESC_CNT] APP_DTCM_BSS;
static int8_t NextBuff = 0;

static uint8_t GetTxBuff_impl(void)
{
    uint8_t Sindex = NextBuff;

    while(1)
    {
        if (((DMATxDscrTab[NextBuff].DESC3 & 0x80000000) == 0) && (tcp_buff_lock[NextBuff].is_locked == 0))
        {
            uint8_t idxr = NextBuff;
            NextBuff = (NextBuff + 1) % ETH_TX_DESC_CNT;
            return idxr;
        }

        NextBuff = (NextBuff + 1) % ETH_TX_DESC_CNT;

        if (NextBuff == Sindex)
        {
            return 255;
        }
    }
}

uint8_t GetTxBuff(void)
{
  uint32_t dwt_start = DWT_START();
  uint8_t index = GetTxBuff_impl();
  DWT_Record(DWT_METRIC_TX_BUFFER_SEARCH, DWT_STOP(dwt_start));
  return index;
}

void Transmit(uint16_t len, uint8_t TXBuffIndex)
{
  uint32_t dwt_start = DWT_START();
    static ETH_BufferTypeDef tx_buffer;

    tx_buffer.buffer = TXbuffer[TXBuffIndex];
    tx_buffer.len = len;
    tx_buffer.next = NULL;

    TxConfig.Length = len;
    TxConfig.TxBuffer = &tx_buffer;
    TxConfig.pData = TXbuffer[TXBuffIndex];

    if (HAL_ETH_Transmit_IT(&heth, &TxConfig) != HAL_OK) {
      tcp_buff_lock[TXBuffIndex].is_locked = 0;
      tcp_buff_lock[TXBuffIndex].retry_count = 0;
    }
  DWT_Record(DWT_METRIC_TX_START, DWT_STOP(dwt_start));
}

void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth)
{
    HAL_ETH_ReleaseTxPacket(heth);
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
