#include "log_writer.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "main.h"
#include "mlvlg.h"

#define LED1 1
#define LED2 2
#define LED_ON 0
#define LED_OFF 1
#define BLINK_INTERVAL_NORMAL 500
#define BLINK_INTERVAL_ERROR 100
#define MAX_FILE_SIZE (4 * 1024 * 1024)
#define MAX_ERROR_COUNT 5
#define LOG_INTERVAL_MS 50  // 20 Hz logging rate

// PoC: 2 fixed fields
#define POC_NUM_FIELDS 2
#define POC_RECORD_LEN 3  // U08(1) + U16(2)

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
static char log_file_name[13];
static uint32_t last_tick_led2 = 0;
static int error_state = 0;
static uint32_t file_counter = 0;
static int error_count = 0;
static uint8_t block_counter = 0;
static uint32_t last_log_tick = 0;
static uint32_t tick_counter = 0;

// Write buffer for serialization
static uint8_t write_buf[256];

static void get_current_datetime(DateTime* dateTime);
static void set_led(int led, int state);
static void toggle_led(int led, uint32_t* last_tick, uint32_t interval);
static FRESULT create_new_log_file(void);
static void set_error_state(FRESULT code);
static FRESULT handle_error(FRESULT res);

// PoC field definitions
static const mlg_Field poc_fields[POC_NUM_FIELDS] = {
  {
    .type = MLG_U08,
    .name = "Counter",
    .units = "count",
    .display_style = MLG_FLOAT,
    .scale = 1.0f,
    .transform = 0.0f,
    .digits = 0,
    .category = "Test"
  },
  {
    .type = MLG_U16,
    .name = "Sine",
    .units = "raw",
    .display_style = MLG_FLOAT,
    .scale = 1.0f,
    .transform = 0.0f,
    .digits = 0,
    .category = "Test"
  },
};

static FRESULT write_mlg_file_header(void) {
  uint32_t data_begin = MLG_HEADER_SIZE + POC_NUM_FIELDS * MLG_FIELD_SIZE;

  mlg_Header header = {
    .file_format = "MLVLG",
    .format_version = 2,
    .timestamp = 0, // no RTC unix time available yet
    .info_data_start = 0,
    .data_begin_index = data_begin,
    .record_length = POC_RECORD_LEN,
    .num_fields = POC_NUM_FIELDS
  };

  // Write header
  int ret = mlg_write_header(write_buf, sizeof(write_buf), &header);
  if (ret < 0) return FR_INT_ERR;

  UINT bw;
  FRESULT res = f_write(&log_file_obj, write_buf, ret, &bw);
  if (res != FR_OK) return res;

  // Write field descriptors
  for (int i = 0; i < POC_NUM_FIELDS; i++) {
    ret = mlg_write_field(write_buf, sizeof(write_buf), &poc_fields[i]);
    if (ret < 0) return FR_INT_ERR;

    res = f_write(&log_file_obj, write_buf, ret, &bw);
    if (res != FR_OK) return res;
  }

  return FR_OK;
}

void lw_init(void) {
  FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK) {
    set_error_state(res);
    return;
  }

  set_led(LED1, LED_ON);
  res = create_new_log_file();
  if (res != FR_OK) {
    set_error_state(res);
  }

  last_log_tick = HAL_GetTick();
}

FRESULT lw_tick(void) {
  if (error_state) {
    toggle_led(LED1, &last_tick_led2, BLINK_INTERVAL_ERROR);
    toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_ERROR);
    return FR_OK;
  }

  toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_NORMAL);

  // Check log interval
  uint32_t now = HAL_GetTick();
  if (now - last_log_tick < LOG_INTERVAL_MS) {
    return FR_OK;
  }
  last_log_tick = now;

  // Generate PoC test data
  uint8_t counter_val = (uint8_t)(tick_counter & 0xFF);
  double angle = (double)tick_counter * 2.0 * 3.14159265358979 / 100.0;
  uint16_t sine_val = (uint16_t)(500.0 + 500.0 * sin(angle));

  // Pack field data in big-endian
  uint8_t data[POC_RECORD_LEN];
  data[0] = counter_val;                   // U08
  data[1] = (sine_val >> 8) & 0xFF;        // U16 BE high
  data[2] = sine_val & 0xFF;               // U16 BE low

  // Timestamp: microseconds / 10
  uint16_t timestamp_10us = (uint16_t)((now * 1000) / 10);  // ms → us → 10us

  int ret = mlg_write_data_block(write_buf, sizeof(write_buf),
                                  block_counter++, timestamp_10us,
                                  data, POC_RECORD_LEN);
  if (ret < 0) return FR_INT_ERR;

  UINT bw;
  FRESULT res = f_write(&log_file_obj, write_buf, ret, &bw);
  if (res != FR_OK) {
    return handle_error(res);
  }

  // Sync periodically (every 100 blocks)
  if (tick_counter % 100 == 0) {
    f_sync(&log_file_obj);
  }

  tick_counter++;

  // File rotation
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
  f_sync(&log_file_obj);
  f_close(&log_file_obj);
  f_mount(0, "", 0);
  set_led(LED1, LED_OFF);
  set_led(LED2, LED_ON);
}

static FRESULT create_new_log_file(void) {
  DateTime dateTime;
  get_current_datetime(&dateTime);
  snprintf(log_file_name, sizeof(log_file_name), "%02d%02d%02d%02d.MLG",
           dateTime.day % 100, dateTime.hour % 100,
           dateTime.minute % 100, (int)(file_counter++ % 100));

  FRESULT res = f_open(&log_file_obj, log_file_name, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) {
    return handle_error(res);
  }

  // Write MLG header + field descriptors
  res = write_mlg_file_header();
  if (res != FR_OK) {
    f_close(&log_file_obj);
    return handle_error(res);
  }

  // Reset block counter for new file
  block_counter = 0;
  tick_counter = 0;

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
  (void)code;
}

static FRESULT handle_error(FRESULT res) {
  error_count++;
  if (error_count >= MAX_ERROR_COUNT) {
    set_error_state(res);
  }
  return res;
}
