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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_INTERVAL_MS   100     // send telemetry every 100ms
#define MATRIX_INTERVAL_MS 2       // refresh one matrix column every 2ms (60Hz)
#define CURRENT_MAX_A      40.0f   // full-scale current for bar graph
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint16_t adc_v_raw = 0;
static uint16_t adc_i_raw = 0;
static float    voltage   = 0.0f;
static float    current   = 0.0f;

static uint32_t last_uart_tick   = 0;
static uint32_t last_matrix_tick = 0;

// LED matrix frame: one byte per column (cols 0-7), n bit = n row
static uint8_t matrix_frame[8] = {0};
static uint8_t current_col     = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void ADC_ReadBothChannels(void);
static void Convert_ADC_to_Physical(void);
static void UART_SendTelemetry(void);
static void Matrix_BuildBarGraph(float value, float max_value);
static void Matrix_Refresh(void);
static void Matrix_ShiftOut595(uint8_t data);
static void Matrix_ShiftOut5916(uint8_t data);
static void Matrix_Latch(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Read V_MEAS (CH0) and A_MEAS (CH1) sequentially by polling
  */
static void ADC_ReadBothChannels(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Rank           = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime   = ADC_SAMPLETIME_239CYCLES_5;

  sConfig.Channel = ADC_CHANNEL_0;
  HAL_ADC_ConfigChannel(&hadc, &sConfig);
  HAL_ADC_Start(&hadc);
  HAL_ADC_PollForConversion(&hadc, 10);
  adc_v_raw = HAL_ADC_GetValue(&hadc);
  HAL_ADC_Stop(&hadc);

  sConfig.Channel = ADC_CHANNEL_1;
  HAL_ADC_ConfigChannel(&hadc, &sConfig);
  HAL_ADC_Start(&hadc);
  HAL_ADC_PollForConversion(&hadc, 10);
  adc_i_raw = HAL_ADC_GetValue(&hadc);
  HAL_ADC_Stop(&hadc);
}

/**
  * @brief Convert raw ADC counts to volts and amps
  *
  * Voltage divider 18k/2k:  V_batt = V_adc * 10
  * ACS772-400B + 22k/33k divider:
  *   V_sensor = V_adc / 0.6
  *   I = (V_sensor - 2.5) / 0.005
  */
static void Convert_ADC_to_Physical(void)
{
  const float vref    = 3.3f;
  const float adc_max = 4095.0f;

  float v_adc = (float)adc_v_raw * vref / adc_max;
  voltage = v_adc * 10.0f;

  float a_adc    = (float)adc_i_raw * vref / adc_max;
  float v_sensor = a_adc / 0.6f;
  current        = (v_sensor - 2.5f) / 0.005f;
  if (current < 0.0f) current = 0.0f;
}

/**
  * @brief Transmit "V:xx.x,I:xx.x,P:xxxx.x\r\n" over UART1 at 115200
  */
static void UART_SendTelemetry(void)
{
  extern UART_HandleTypeDef huart1;
  char buf[64];
  int  len = snprintf(buf, sizeof(buf), "V:%.1f,I:%.1f,P:%.1f\r\n",
                      voltage, current, voltage * current);
  HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 20);
}

/**
  * @brief Fill matrix_frame as a bar graph (0..64 LEDs = 0..max_value)
  *        Columns fill left to right; within each column rows fill bottom-up
  */
static void Matrix_BuildBarGraph(float value, float max_value)
{
  if (value < 0.0f)       value = 0.0f;
  if (value > max_value)  value = max_value;
  int total_on = (int)((value / max_value) * 64.0f);

  for (int col = 0; col < 8; col++)
  {
    int leds = total_on - (col * 8);
    if      (leds <= 0) matrix_frame[col] = 0x00;
    else if (leds >= 8) matrix_frame[col] = 0xFF;
    else                matrix_frame[col] = (uint8_t)(0xFF << (8 - leds));
  }
}

/**
  * @brief Drive one column per call — must be called every MATRIX_INTERVAL_MS
  *        74AHC595 selects the active column anode (one-hot via NPN)
  *        TLC5916 sinks the active row cathodes
  */
static void Matrix_Refresh(void)
{
  Matrix_ShiftOut595(1u << current_col);
  Matrix_ShiftOut5916(matrix_frame[current_col]);
  Matrix_Latch();
  current_col = (current_col + 1u) & 0x07u;
}

/* Shift 8 bits into 74AHC595: SER_DATA=PA4, SHIFT_CLK=PA6, MSB first */
static void Matrix_ShiftOut595(uint8_t data)
{
  for (int i = 7; i >= 0; i--)
  {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4,
        (data >> i) & 1u ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
  }
}

/* Shift 8 bits into TLC5916: SDI_DATA=PA5, SHIFT_CLK=PA6, MSB first */
static void Matrix_ShiftOut5916(uint8_t data)
{
  for (int i = 7; i >= 0; i--)
  {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5,
        (data >> i) & 1u ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
  }
}

/* Pulse OUTPUT_CLOCK (PA7) to latch both shift registers simultaneously */
static void Matrix_Latch(void)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
}

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
  MX_ADC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  // Flash status LED to confirm boot
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
  HAL_Delay(300);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    // Telemetry: read sensors and send UART every 100ms
    if (now - last_uart_tick >= UART_INTERVAL_MS)
    {
      last_uart_tick = now;
      ADC_ReadBothChannels();
      Convert_ADC_to_Physical();
      UART_SendTelemetry();
      HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
      Matrix_BuildBarGraph(current, CURRENT_MAX_A);      // updates bar graph
    }

    // Matrix multiplex: advance one column every 2ms
    if (now - last_matrix_tick >= MATRIX_INTERVAL_MS)
    {
      last_matrix_tick = now;
      Matrix_Refresh();
    }

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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI14;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC_Init(void)
{

  /* USER CODE BEGIN ADC_Init 0 */

  /* USER CODE END ADC_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC_Init 1 */

  /* USER CODE END ADC_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc.Instance = ADC1;
  hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.LowPowerAutoPowerOff = DISABLE;
  hadc.Init.ContinuousConvMode = DISABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  if (HAL_ADC_Init(&hadc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC_Init 2 */

  /* USER CODE END ADC_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA4 PA5 PA6 PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
