/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
extern RTC_HandleTypeDef hrtc;
/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  RTC_DateTypeDef cur_date;
  RTC_TimeTypeDef cur_time;

  HAL_RTC_GetTime(&hrtc, &cur_time, RTC_FORMAT_BCD);
  HAL_RTC_GetDate(&hrtc, &cur_date, RTC_FORMAT_BCD);

  uint16_t year = 2000 + (cur_date.Year >> 4) * 10 + (cur_date.Year & 0x0F);
  uint8_t month = (cur_date.Month >> 4) * 10 + (cur_date.Month & 0x0F);
  uint8_t day = (cur_date.Date >> 4) * 10 + (cur_date.Date & 0x0F);
  uint8_t hour = (cur_time.Hours >> 4) * 10 + (cur_time.Hours & 0x0F);
  uint8_t minute = (cur_time.Minutes >> 4) * 10 + (cur_time.Minutes & 0x0F);
  uint8_t second = (cur_time.Seconds >> 4) * 10 + (cur_time.Seconds & 0x0F);

  return ((DWORD)(year - 1980) << 25) |
         ((DWORD)month << 21) |
         ((DWORD)day << 16) |
         ((DWORD)hour << 11) |
         ((DWORD)minute << 5) |
         ((DWORD)(second / 2));
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
