#include "log_writer.h"

#include <stdio.h>
#include <string.h>
#include "main.h"

#define LED1 1
#define LED2 2
#define LED_ON 0  // Логический 0 для включения светодиодов
#define LED_OFF 1  // Логический 1 для выключения светодиодов
#define BLINK_INTERVAL_NORMAL 500
#define BLINK_INTERVAL_ERROR 100
#define MAX_FILE_SIZE 4 * 1024 * 1024  // Приватная константа для максимального размера файла
#define MAX_ERROR_COUNT 5  // Приватная константа для максимального числа неудачных попыток

extern RTC_HandleTypeDef hrtc;

typedef struct {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} DateTime;

static FIL log_file_obj;
static uint32_t bytes_written;
static char log_file_name[13]; // Формат 8.3
static uint32_t last_tick_led1 = 0;
static uint32_t last_tick_led2 = 0;
static int error_state = 0;
static FRESULT error_code = FR_OK;
static uint32_t file_counter = 0;
static int error_count = 0;

#define FIELDS_SIZE 1
//mlg_LoggerField_Scalar fields[FIELDS_SIZE] = {
//    {MLG_U08, "U08Field", "U08", MLG_FLOAT, 1.0f, 0.0f, 2, "Category"},
//};
//const char* infoData = "Additional Info Data";
//uint8_t buffer[MAX_BUFFER_SIZE];

static void get_current_datetime(DateTime* dateTime);
static void set_led(int led, int state);
static void toggle_led(int led, uint32_t* last_tick, uint32_t interval);
static FRESULT create_new_log_file(void);
static void set_error_state(FRESULT code);
static FRESULT handle_error(FRESULT res);

void lw_init(void) {
  FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK) {
    set_error_state(res);
    return;
  }

  set_led(LED1, LED_ON);  // LED1 горит
  res = create_new_log_file();
  if (res != FR_OK) {
    set_error_state(res);
  }
}

FRESULT lw_tick(void) {
  if (error_state) {
    toggle_led(LED1, &last_tick_led1, BLINK_INTERVAL_ERROR);  // LED1 мигает при ошибке
    toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_ERROR);  // LED2 мигает при ошибке
    return FR_OK;
  }

  toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_NORMAL);  // LED2 мигает в штатном режиме

  ////////////////////////////////////////////////////////

//  size_t dataSize = mlg_calculateDataSize(fields, FIELDS_SIZE, 0, 0);
//  uint8_t* data = (uint8_t*)malloc(dataSize);
//  if (data == NULL) {
//    // Failed to allocate memory for data
//    return FR_INT_ERR;
//  }
//
//  for (size_t i = 0; i < dataSize; i++) {
//    data[i] = i + 1;
//  }
//
//  mlg_DataBlock dataBlock = {0, 1, HAL_GetTick(), data, 0};
//
//  uint8_t dataBlockBuffer[256];
//  mlg_PackDataBlockArgs dataBlockArgs = {
//      .dataBlock = &dataBlock,
//      .scalarFields = fields,
//      .numScalarFields = FIELDS_SIZE,
//      .buffer = dataBlockBuffer,
//      .bufferSize = sizeof(dataBlockBuffer)
//  };
//
//  int result = mlg_packDataBlock(&dataBlockArgs);
//
//  if (result == -1) {
//    // Data block buffer overflow
//    free(data);
//    return FR_INT_ERR;
//  }
//
//  uint32_t dataBlockSize = 1 + 1 + 2 + dataSize + 1; // Type + Counter + Timestamp + Data + CRC

  ////////////////////////////////////////////////////////

//  FRESULT res = f_write(&log_file_obj, dataBlockBuffer, dataBlockSize, (void *)&bytes_written);
  FRESULT res = FR_OK;

  if (res != FR_OK) {
    return handle_error(res);
  }

  if (f_size(&log_file_obj) >= MAX_FILE_SIZE) {
    f_close(&log_file_obj);
    res = create_new_log_file();
    if (res != FR_OK) {
      return handle_error(res);
    }
  }

  return FR_OK;
}

void lw_stop(void) {
  f_close(&log_file_obj);
  f_mount(0, "", 0);
  set_led(LED1, LED_OFF);  // LED1 выключен
  set_led(LED2, LED_ON);   // LED2 горит при остановке
}

static FRESULT create_new_log_file(void) {
  DateTime dateTime;
  get_current_datetime(&dateTime);
  sprintf(log_file_name, "%02d%02d%02d%02d.MLG", dateTime.day, dateTime.hour, dateTime.minute, file_counter++ % 100);
  FRESULT res = f_open(&log_file_obj, log_file_name, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) {
    return handle_error(res);
  }

//  // create header
//  mlg_Header header = {
//      .fileFormat = "MLVLG",
//      .formatVersion = 0x0002,
//      .timeStamp = 0x5F4D3C2B,  // Example timestamp
//      .infoDataStart = 0x00000000,
//      .dataBeginIndex = 0x00000010, // This will be calculated dynamically
//      .recordLength = 0x0020,
//      .numLoggerFields = 8 // This will be calculated dynamically
//  };
//
//  memset(buffer, 0, MAX_BUFFER_SIZE);
//
//  mlg_PackHeaderArgs headerArgs = {
//      .header = &header,
//      .scalarFields = fields,
//      .numScalarFields = FIELDS_SIZE,
//      .buffer = buffer,
//      .bufferSize = MAX_BUFFER_SIZE,
//      .infoDataStartOffset = sizeof(mlg_Header) + 1 * sizeof(mlg_LoggerField_Scalar) + 1
//  };
//
//  int result = mlg_packHeaderToBuffer(&headerArgs);
//
//  if (result != 0) {
//    // Header buffer overflow
//    return FR_INT_ERR;
//  }
//
//  size_t infoDataLength = strlen(infoData) + 1; // Include null terminator
//  if (infoDataLength + header.dataBeginIndex > MAX_BUFFER_SIZE) {
//    // Info Data buffer overflow
//    return FR_INT_ERR;
//  }
//  memcpy(buffer + header.dataBeginIndex, infoData, infoDataLength);
//
//  res = f_write(&log_file_obj, buffer, header.dataBeginIndex + infoDataLength, (void *)&bytes_written);
  if (res != FR_OK) {
    return handle_error(res);
  }

  return FR_OK;
}

static void get_current_datetime(DateTime* dateTime) {
  RTC_DateTypeDef cur_date;
  RTC_TimeTypeDef cur_time;
  HAL_RTC_GetTime(&hrtc, &cur_time, RTC_FORMAT_BCD);
  HAL_RTC_GetDate(&hrtc, &cur_date, RTC_FORMAT_BCD);
  dateTime->year = (cur_date.Year >> 4) * 10 + (cur_date.Year & 0x0F);
  dateTime->month = (cur_date.Month >> 4) * 10 + (cur_date.Month & 0x0F);
  dateTime->day = (cur_date.Date >> 4) * 10 + (cur_date.Date & 0x0F);
  dateTime->hour = (cur_time.Hours >> 4) * 10 + (cur_time.Hours & 0x0F);
  dateTime->minute = (cur_time.Minutes >> 4) * 10 + (cur_time.Minutes & 0x0F);
  dateTime->second = (cur_time.Seconds >> 4) * 10 + (cur_time.Seconds & 0x0F);
}

static void set_led(int led, int state) {
  GPIO_PinState pin_state = (state == LED_ON) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  if (led == LED1) {
    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, pin_state);
  } else if (led == LED2) {
    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, pin_state);
  }
}

static void toggle_led(int led, uint32_t* last_tick, uint32_t interval) {
  if (HAL_GetTick() - *last_tick >= interval) {
    if (led == LED1) {
      HAL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin);
    } else if (led == LED2) {
      HAL_GPIO_TogglePin(LED_2_GPIO_Port, LED_2_Pin);
    }
    *last_tick = HAL_GetTick();
  }
}

static void set_error_state(FRESULT code) {
  error_state = 1;
  error_code = code;
}

static FRESULT handle_error(FRESULT res) {
  error_count++;
  if (error_count >= MAX_ERROR_COUNT) {
    set_error_state(res);
  }
  return res;
}
