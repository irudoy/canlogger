#include "log_writer.h"

#include <stdio.h>
#include <string.h>
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
#define CONFIG_FILE_NAME "config.ini"
#define CONFIG_BUF_SIZE 4096

extern RTC_HandleTypeDef hrtc;

typedef struct {
  uint8_t year, month, day, hour, minute, second;
} DateTime;

static FIL log_file_obj;
static char log_file_name[13];
static uint32_t last_tick_led1 = 0;
static uint32_t last_tick_led2 = 0;
static int error_state = 0;
static uint32_t file_counter = 0;
static int error_count = 0;
static uint8_t block_counter = 0;
static uint32_t last_log_tick = 0;

// Cached MLG fields (built from config at init)
static mlg_Field mlg_fields[CFG_MAX_FIELDS];
static uint16_t num_mlg_fields = 0;
static size_t record_length = 0;

static uint8_t write_buf[512];
static char config_buf[CONFIG_BUF_SIZE];

static void get_current_datetime(DateTime* dt);
static uint32_t datetime_to_unix(const DateTime* dt);
static void set_led(int led, int state);
static void toggle_led(int led, uint32_t* last_tick, uint32_t interval);
static FRESULT create_new_log_file(void);
static void set_error_state(void);
static FRESULT handle_error(FRESULT res);

static FRESULT write_mlg_file_header(void) {
  uint32_t data_begin = MLG_HEADER_SIZE + num_mlg_fields * MLG_FIELD_SIZE;

  DateTime dt;
  get_current_datetime(&dt);

  mlg_Header header = {
    .file_format = "MLVLG",
    .format_version = 2,
    .timestamp = datetime_to_unix(&dt),
    .info_data_start = 0,
    .data_begin_index = data_begin,
    .record_length = (uint16_t)record_length,
    .num_fields = num_mlg_fields
  };

  int ret = mlg_write_header(write_buf, sizeof(write_buf), &header);
  if (ret < 0) return FR_INT_ERR;

  UINT bw;
  FRESULT res = f_write(&log_file_obj, write_buf, ret, &bw);
  if (res != FR_OK) return res;

  for (int i = 0; i < num_mlg_fields; i++) {
    ret = mlg_write_field(write_buf, sizeof(write_buf), &mlg_fields[i]);
    if (ret < 0) return FR_INT_ERR;
    res = f_write(&log_file_obj, write_buf, ret, &bw);
    if (res != FR_OK) return res;
  }

  return FR_OK;
}

int lw_init(cfg_Config* cfg_out, can_FieldValues* fv_out) {
  // Mount SD
  FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK) {
    set_error_state();
    return -1;
  }

  // Read config file
  FIL cfg_file;
  res = f_open(&cfg_file, CONFIG_FILE_NAME, FA_READ);
  if (res != FR_OK) {
    set_error_state();
    return -1;
  }

  UINT bytes_read;
  res = f_read(&cfg_file, config_buf, CONFIG_BUF_SIZE - 1, &bytes_read);
  f_close(&cfg_file);
  if (res != FR_OK) {
    set_error_state();
    return -1;
  }
  config_buf[bytes_read] = '\0';

  // Parse config
  int parse_ret = cfg_parse(config_buf, bytes_read, cfg_out);
  if (parse_ret != CFG_OK) {
    set_error_state();
    return -1;
  }

  // Build MLG fields from config
  num_mlg_fields = cfg_out->num_fields;
  can_map_build_mlg_fields(cfg_out, mlg_fields, CFG_MAX_FIELDS);

  // Init field values shadow buffer
  if (can_map_init(fv_out, cfg_out) != 0) {
    set_error_state();
    return -1;
  }
  record_length = fv_out->record_length;

  // Create first log file
  set_led(LED1, LED_ON);
  res = create_new_log_file();
  if (res != FR_OK) {
    set_error_state();
    return -1;
  }

  last_log_tick = HAL_GetTick();
  return 0;
}

FRESULT lw_tick(const can_FieldValues* fv, uint32_t log_interval_ms) {
  if (error_state) return FR_OK;

  uint32_t now = HAL_GetTick();
  if (now - last_log_tick < log_interval_ms) {
    return FR_OK;
  }
  last_log_tick = now;

  // Timestamp: ms → 10us units (use uint64_t to avoid overflow at ~1.2 hours)
  uint16_t timestamp_10us = (uint16_t)(((uint64_t)now * 100) & 0xFFFF);

  int ret = mlg_write_data_block(write_buf, sizeof(write_buf),
                                  block_counter++, timestamp_10us,
                                  fv->values, fv->record_length);
  if (ret < 0) return FR_INT_ERR;

  UINT bw;
  FRESULT res = f_write(&log_file_obj, write_buf, ret, &bw);
  if (res != FR_OK) {
    return handle_error(res);
  }

  // Sync periodically
  static uint32_t sync_counter = 0;
  if (++sync_counter % 100 == 0) {
    f_sync(&log_file_obj);
  }

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

void lw_update_leds(void) {
  if (error_state) {
    toggle_led(LED1, &last_tick_led1, BLINK_INTERVAL_ERROR);
    toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_ERROR);
  } else {
    toggle_led(LED2, &last_tick_led2, BLINK_INTERVAL_NORMAL);
  }
}

int lw_is_error(void) {
  return error_state;
}

void lw_get_status(lw_Status* out) {
  out->file_name   = log_file_name;
  out->file_size   = error_state ? 0 : f_size(&log_file_obj);
  out->file_count  = file_counter;
  out->error_count = error_count;
  out->error_state = error_state;
  out->block_count = block_counter;
}

static FRESULT create_new_log_file(void) {
  DateTime dt;
  get_current_datetime(&dt);
  snprintf(log_file_name, sizeof(log_file_name), "%02d%02d%02d%02d.MLG",
           dt.day % 100, dt.hour % 100, dt.minute % 100,
           (int)(file_counter++ % 100));

  FRESULT res = f_open(&log_file_obj, log_file_name, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return handle_error(res);

  res = write_mlg_file_header();
  if (res != FR_OK) {
    f_close(&log_file_obj);
    return handle_error(res);
  }

  block_counter = 0;
  return FR_OK;
}

static uint32_t datetime_to_unix(const DateTime* dt) {
  // Days from 1970-01-01 to 2000-01-01 = 10957
  uint32_t year = dt->year + 2000;
  uint32_t days = (year - 1970) * 365 + ((year - 1969) / 4);
  static const uint16_t mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (dt->month >= 1 && dt->month <= 12) {
    days += mdays[dt->month - 1];
  }
  // Leap year correction for current year
  if (dt->month > 2 && (year % 4 == 0)) days++;
  days += dt->day - 1;
  return days * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second;
}

static void get_current_datetime(DateTime* dt) {
  RTC_DateTypeDef d;
  RTC_TimeTypeDef t;
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BCD);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BCD);
  dt->year   = (d.Year >> 4) * 10 + (d.Year & 0x0F);
  dt->month  = (d.Month >> 4) * 10 + (d.Month & 0x0F);
  dt->day    = (d.Date >> 4) * 10 + (d.Date & 0x0F);
  dt->hour   = (t.Hours >> 4) * 10 + (t.Hours & 0x0F);
  dt->minute = (t.Minutes >> 4) * 10 + (t.Minutes & 0x0F);
  dt->second = (t.Seconds >> 4) * 10 + (t.Seconds & 0x0F);
}

static void set_led(int led, int state) {
  GPIO_PinState ps = (state == LED_ON) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  if (led == LED1) HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, ps);
  else if (led == LED2) HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, ps);
}

static void toggle_led(int led, uint32_t* last_tick, uint32_t interval) {
  if (HAL_GetTick() - *last_tick >= interval) {
    if (led == LED1) HAL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin);
    else if (led == LED2) HAL_GPIO_TogglePin(LED_2_GPIO_Port, LED_2_Pin);
    *last_tick = HAL_GetTick();
  }
}

static void set_error_state(void) {
  error_state = 1;
}

static FRESULT handle_error(FRESULT res) {
  error_count++;
  if (error_count >= MAX_ERROR_COUNT) set_error_state();
  return res;
}
