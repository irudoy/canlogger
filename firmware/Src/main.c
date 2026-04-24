/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "cmsis_os.h"
#include "fatfs.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "log_writer.h"
#include "can_drv.h"
#include "ring_buf.h"
#include "can_map.h"
#include "debug_out.h"
#include "demo_gen.h"
#include "demo_can.h"
#include "vin_sense.h"
#include "bkp_log.h"
#include "gps_drv.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define VIN_SENSE_CHECK_MS    100   /* poll interval */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

CAN_HandleTypeDef hcan1;

RTC_HandleTypeDef hrtc;

SD_HandleTypeDef hsd;
DMA_HandleTypeDef hdma_sdio_rx;
DMA_HandleTypeDef hdma_sdio_tx;

UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
volatile uint8_t lw_shutdown = 0;
volatile uint8_t sd_stopped = 0;  // set by task_sd after lw_stop completes
volatile uint8_t vin_triggered_shutdown = 0;  // 1 = VIN outage (auto-resume), 0 = CDC `stop`
// Marker request: 0=none, 1=K0 press ("btn"), 2=CDC `mark` (marker_cdc_text).
// Set by EXTI ISR or CDC task, consumed by task_sd.
volatile uint8_t marker_request = 0;
char marker_cdc_text[50];  // filled by CDC before setting marker_request=2
static ring_Buffer can_rx_buf;
static cfg_Config config __attribute__((section(".ccmram")));
static can_FieldValues field_values;
static uint32_t can_frames_processed = 0;
volatile int init_ok = 0;
// Demo mode helper arrays (extracted from config for demo_generate)
static uint8_t demo_field_types[CFG_MAX_FIELDS];
static float demo_field_scales[CFG_MAX_FIELDS];
static float demo_field_offsets[CFG_MAX_FIELDS];

// FreeRTOS: shadow mutex guards field_values between task_producer and task_sd
osMutexId_t shadow_mutex;
static const osMutexAttr_t shadow_mutex_attr = {
  .name = "shadowMutex"
};

// FreeRTOS: SD writer task
osThreadId_t sdTaskHandle;
static const osThreadAttr_t sdTask_attributes = {
  .name = "sdTask",
  .stack_size = 4096,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
void SdTaskEntry(void *argument);
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_CAN1_Init(void);
static void MX_SDIO_SD_Init(void);
static void MX_RTC_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART3_UART_Init(void);
void StartDefaultTask(void *argument);

static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == USR_BTN_3_K1_Pin) {
    lw_shutdown = 1;
    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET); // D2 off (active-low)
  } else if (GPIO_Pin == USR_BTN_4_K0_Pin) {
    // Debounce: any edge (including release-bounce that also trips FALLING)
    // extends the blackout. Window must exceed typical hold + release time.
    static uint32_t k0_last_tick = 0;
    uint32_t now = HAL_GetTick();
    uint32_t since = now - k0_last_tick;
    k0_last_tick = now;
    if (since < 300) return;
    if (marker_request == 0) marker_request = 1;  // drop if SD hasn't consumed yet
  }
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
  MX_DMA_Init();
  MX_CAN1_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_USART3_UART_Init();

  /* Initialize interrupts */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */
  bkp_log_init();  // increment session counter in BKP_DR1
  ring_buf_init(&can_rx_buf);
  /* SD/FatFS init moved to StartDefaultTask — requires scheduler running
   * (RTOS sd_diskio template checks osKernelGetState before BSP_SD_Init). */
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  shadow_mutex = osMutexNew(&shadow_mutex_attr);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  sdTaskHandle = osThreadNew(SdTaskEntry, NULL, &sdTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* Dead code — scheduler never returns. Kept for CubeMX marker compliance. */
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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

/**
  * @brief NVIC Configuration.
  * @retval None
  */
static void MX_NVIC_Init(void)
{
  /* CAN1_RX0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_6TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_7TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */
  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  // RTC_ISR_INITS is hw-set on first successful SetTime and cleared only on
  // backup domain reset (VBAT loss). Survives flash/reset — no bootstrap on
  // warm path. Time is set manually via CDC `settime` or GPS when available.
  if (__HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc)) {
    return;
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x18;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_WEDNESDAY;
  sDate.Month = RTC_MONTH_JULY;
  sDate.Date = 0x17;
  sDate.Year = 0x24;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  // Reached only on fresh chip or VBAT loss (INITS flag not set).
  // Bootstrap with neutral date; user sets real time via CDC `settime`.
  sTime.Hours = 0x00;
  sTime.Minutes = 0x00;
  sTime.Seconds = 0x00;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK) {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_THURSDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x01;
  sDate.Year = 0x26;  // 2026
  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SDIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDIO_SD_Init(void)
{

  /* USER CODE BEGIN SDIO_Init 0 */

  /* USER CODE END SDIO_Init 0 */

  /* USER CODE BEGIN SDIO_Init 1 */

  /* USER CODE END SDIO_Init 1 */
  hsd.Instance = SDIO;
  hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
  hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
  hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
  hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
  hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd.Init.ClockDiv = 4;
  /* USER CODE BEGIN SDIO_Init 2 */

  /* USER CODE END SDIO_Init 2 */

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
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
  /* DMA2_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  /* DMA2_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_1_Pin|LED_2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USR_BTN_3_K1_Pin */
  GPIO_InitStruct.Pin = USR_BTN_3_K1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(USR_BTN_3_K1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USR_BTN_4_K0_Pin */
  GPIO_InitStruct.Pin = USR_BTN_4_K0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(USR_BTN_4_K0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_1_Pin LED_2_Pin */
  GPIO_InitStruct.Pin = LED_1_Pin|LED_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void SdTaskEntry(void *argument)
{
  (void)argument;
  static uint8_t snapshot[CAN_MAP_MAX_RECORD_SIZE];
  uint32_t next_wake = 0;

  while (!lw_shutdown) {
    if (!init_ok || lw_is_error()) {
      osDelay(100);
      next_wake = 0;  // reset periodic base after wait
      continue;
    }

    // Periodic timing via osDelayUntil — drift-free
    if (next_wake == 0) {
      next_wake = osKernelGetTickCount();
    }
    next_wake += config.log_interval_ms;
    osDelayUntil(next_wake);

    if (lw_shutdown) break;

    // Snapshot shadow buffer under mutex
    osMutexAcquire(shadow_mutex, osWaitForever);
    size_t rec_len = field_values.record_length;
    memcpy(snapshot, field_values.values, rec_len);
    osMutexRelease(shadow_mutex);

    // Marker drained before snapshot so it lands at the chronologically
    // correct spot between data blocks.
    if (marker_request) {
      const char* msg = (marker_request == 2) ? marker_cdc_text : "btn";
      lw_write_marker(msg);
      marker_request = 0;
    }

    lw_write_snapshot(snapshot, rec_len);
  }

  // Graceful shutdown: flush and close
  if (init_ok) lw_stop();

  // Signal task_producer that SD is fully flushed — safe to reset MCU now.
  sd_stopped = 1;
  osThreadExit();
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 5 */
  // Init SD, read config, write MLG header (must run after scheduler start —
  // RTOS sd_diskio template requires osKernelRunning for BSP_SD_Init)
  init_ok = (lw_init(&config, &field_values) == 0);

  gps_drv_init();

  // Extract field metadata for demo generator
  if (init_ok && config.demo) {
    for (int i = 0; i < config.num_fields; i++) {
      demo_field_types[i] = config.fields[i].type;
      demo_field_scales[i] = config.fields[i].scale;
      demo_field_offsets[i] = config.fields[i].offset;
    }
  }

  if (init_ok) {
    if (config.num_can_ids > 0) {
      can_drv_init(&can_rx_buf, &config);
      can_drv_start();
    }
  } else {
    config.can_bitrate = 500000;
    config.num_can_ids = 0;
    can_drv_init(&can_rx_buf, &config);
    can_drv_start();
  }

  /* task_producer: drain CAN ring buffer, update shadow, handle debug CLI */
  for(;;)
  {
    if (lw_shutdown) {
      can_drv_stop();
      // Wait for task_sd to flush SD before MCU reset — resetting mid-write
      // corrupts the file. Cap wait at 2s: if lw_stop wedges (card removed,
      // HAL stuck), reset anyway so we don't drain the supercap forever.
      uint32_t deadline = HAL_GetTick() + 2000;
      while (!sd_stopped && HAL_GetTick() < deadline) osDelay(10);

      // CDC `stop` (vin_triggered_shutdown=0) just halts — user wants to
      // flash or remove the card. VIN-triggered shutdown waits for power
      // to return, then resets so logging auto-resumes.
      if (!vin_triggered_shutdown) osThreadExit();

      int high_count = 0;
      for (;;) {
        osDelay(VIN_SENSE_CHECK_MS);
        vin_sense_poll();
        if (vin_sense_mv >= VIN_SENSE_THRESHOLD_MV) {
          if (++high_count >= 3) NVIC_SystemReset();
        } else {
          high_count = 0;
        }
      }
    }

    // Pack demo data into CAN frames (if demo + ring buf mode)
    if (init_ok && config.demo && config.demo_gen.use_ring_buf) {
      demo_pack_can_frames(&config, &can_rx_buf, HAL_GetTick());
    }

    // Drain CAN ring buffer → update shadow under mutex
    {
      can_Frame frame;
      osMutexAcquire(shadow_mutex, osWaitForever);
      gps_drv_poll();
      if (init_ok && config.gps_enabled) {
        can_map_update_gps(&field_values, &config, gps_drv_state());
      }
      while (ring_buf_pop(&can_rx_buf, &frame) == 0) {
        if (init_ok) can_map_process(&field_values, &config, &frame);
        can_frames_processed++;
        debug_out_set_can(frame.id, frame.data, frame.dlc);
      }
      // Direct demo for fields without CAN IDs (legacy path)
      if (init_ok && config.demo && !config.demo_gen.use_ring_buf) {
        demo_generate(&config.demo_gen,
                      field_values.values, field_values.record_length,
                      demo_field_types, demo_field_scales, demo_field_offsets,
                      config.num_fields, HAL_GetTick());
        field_values.updated = 1;
      }
      osMutexRelease(shadow_mutex);
    }

    // One-shot RTC sync on first GPS fix with date+time. `gps_rtc_synced`
    // latches for the life of this boot so the loop doesn't keep re-setting
    // the RTC every iteration. CDC `settime` still works as a manual override.
    {
      static uint8_t gps_rtc_synced = 0;
      if (!gps_rtc_synced) {
        const gps_State* gs = gps_drv_state();
        if (gs->has_fix && gs->has_date && gs->has_time) {
          #define BCD(v) (uint8_t)(((v) / 10) << 4 | ((v) % 10))
          RTC_TimeTypeDef t = {
            .Hours = BCD(gs->hour), .Minutes = BCD(gs->minute), .Seconds = BCD(gs->second),
            .DayLightSaving = RTC_DAYLIGHTSAVING_NONE,
            .StoreOperation = RTC_STOREOPERATION_RESET,
          };
          RTC_DateTypeDef d = {
            .WeekDay = RTC_WEEKDAY_MONDAY,
            .Month = BCD(gs->month), .Date = BCD(gs->day),
            .Year = BCD(gs->year % 100),
          };
          #undef BCD
          if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BCD) == HAL_OK &&
              HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD) == HAL_OK) {
            gps_rtc_synced = 1;
          }
        }
      }
    }

    debug_out_tick(can_frames_processed, config.num_fields, init_ok);
    debug_cmd_poll(&config, init_ok, &can_rx_buf);
    lw_update_leds();

    // VIN_SENSE: check every VIN_SENSE_CHECK_MS
    {
      static uint32_t next_vin_check = 0;
      uint32_t now = HAL_GetTick();
      if (now >= next_vin_check) {
        vin_sense_poll();
        next_vin_check = now + VIN_SENSE_CHECK_MS;
      }
    }

    osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
