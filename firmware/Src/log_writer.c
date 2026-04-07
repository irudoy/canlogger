#include "log_writer.h"

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "mlvlg.h"
#include "sd_write_dma.h"

#define LED1 1
#define LED2 2
#define LED_ON 0
#define LED_OFF 1
#define BLINK_INTERVAL_NORMAL 500
#define BLINK_INTERVAL_ERROR 100
#define MAX_FILE_SIZE (512 * 1024 * 1024)
#define MAX_ERROR_COUNT 5
#define CONFIG_FILE_NAME "config.ini"
#define CONFIG_BUF_SIZE 8192

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
static uint32_t recovery_count = 0;
static FRESULT last_error = FR_OK;
static const char* last_error_at = "";
static uint8_t block_counter = 0;
static uint32_t last_log_tick = 0;

// Cached MLG fields (built from config at init)
static mlg_Field mlg_fields[CFG_MAX_FIELDS];
static uint16_t num_mlg_fields = 0;
static size_t record_length = 0;

static uint8_t write_buf[512] __attribute__((aligned(4)));
static char config_buf[CONFIG_BUF_SIZE];

// I/O buffer: accumulate records, write to SD in larger chunks
#define IO_BUF_SIZE 4096
static uint8_t io_buf[IO_BUF_SIZE] __attribute__((aligned(4)));
static uint16_t io_pos = 0;

static void get_current_datetime(DateTime* dt);
static uint32_t datetime_to_unix(const DateTime* dt);
static void set_led(int led, int state);
static void toggle_led(int led, uint32_t* last_tick, uint32_t interval);
static FRESULT create_new_log_file(void);
static void set_error_state(FRESULT res, const char* at);
static FRESULT flush_io_buf(void);
static FRESULT recover_file(void);
static FRESULT handle_error(FRESULT res, const char* at);

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
    set_error_state(res, "mount");
    return -1;
  }

  // Read config file
  FIL cfg_file;
  res = f_open(&cfg_file, CONFIG_FILE_NAME, FA_READ);
  if (res != FR_OK) {
    set_error_state(res, "cfg_open");
    return -1;
  }

  UINT bytes_read;
  res = f_read(&cfg_file, config_buf, CONFIG_BUF_SIZE - 1, &bytes_read);
  f_close(&cfg_file);
  if (res != FR_OK) {
    set_error_state(res, "cfg_read");
    return -1;
  }
  config_buf[bytes_read] = '\0';

  // Parse config
  int parse_ret = cfg_parse(config_buf, bytes_read, cfg_out);
  if (parse_ret != CFG_OK) {
    set_error_state(FR_INT_ERR, "cfg_parse");
    return -1;
  }

  // Build MLG fields from config
  num_mlg_fields = cfg_out->num_fields;
  can_map_build_mlg_fields(cfg_out, mlg_fields, CFG_MAX_FIELDS);

  // Init field values shadow buffer
  if (can_map_init(fv_out, cfg_out) != 0) {
    set_error_state(FR_INT_ERR, "can_init");
    return -1;
  }
  record_length = fv_out->record_length;

  // Create first log file
  set_led(LED1, LED_ON);
  res = create_new_log_file();
  if (res != FR_OK) {
    set_error_state(res, "create");
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

  // Accumulate in I/O buffer, flush when full
  if (io_pos + ret > IO_BUF_SIZE) {
    FRESULT res = flush_io_buf();
    if (res != FR_OK) return res;
  }
  memcpy(io_buf + io_pos, write_buf, ret);
  io_pos += ret;

  // Sync periodically (every 100 blocks)
  static uint32_t sync_counter = 0;
  if (++sync_counter % 100 == 0) {
    FRESULT res = flush_io_buf();
    if (res != FR_OK) return res;
    res = f_sync(&log_file_obj);
    if (res != FR_OK) {
      return handle_error(res, "sync");
    }
  }

  // File rotation (f_tell for actual position, f_size is MAX_FILE_SIZE after f_expand)
  if (f_tell(&log_file_obj) + io_pos >= MAX_FILE_SIZE) {
    FRESULT res = flush_io_buf();
    if (res != FR_OK) return res;
    f_truncate(&log_file_obj);
    f_close(&log_file_obj);
    res = create_new_log_file();
    if (res != FR_OK) {
      return handle_error(res, "rotate");
    }
  }

  return FR_OK;
}

void lw_stop(void) {
  flush_io_buf();
  f_sync(&log_file_obj);
  f_truncate(&log_file_obj);
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
  out->file_size   = error_state ? 0 : f_tell(&log_file_obj);
  out->file_count  = file_counter;
  out->error_count = error_count;
  out->error_state = error_state;
  out->last_error = last_error;
  out->last_error_at = last_error_at;
  out->recovery_count = recovery_count;
  out->block_count = block_counter;

  sd_ErrorCounters ec;
  sd_get_error_counters(&ec);
  out->sd_cmd_timeout   = ec.cmd_rsp_timeout;
  out->sd_data_timeout  = ec.data_timeout;
  out->sd_data_crc_fail = ec.data_crc_fail;
  out->sd_dma_error     = ec.dma_error;
  out->sd_err_callbacks = ec.total_callbacks;
  out->sd_last_err_code = ec.last_error_code;
  out->sd_hal_err_code  = ec.hal_error_code;
}

#define RECOVERY_DELAY_MS 1000

static FRESULT recover_file(void) {
  f_close(&log_file_obj);
  // Remount SD — card may need full re-init after GC stall
  f_mount(0, "", 0);
  HAL_Delay(RECOVERY_DELAY_MS);
  FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK) return res;
  res = create_new_log_file();
  if (res == FR_OK) {
    recovery_count++;
    error_count = 0;
  }
  return res;
}

static FRESULT flush_io_buf(void) {
  if (io_pos == 0) return FR_OK;
  UINT bw;
  FRESULT res = f_write(&log_file_obj, io_buf, io_pos, &bw);
  if (res == FR_OK) {
    io_pos = 0;
    return FR_OK;
  }
  // Write failed — file object may be invalid, try recovery
  uint16_t saved_pos = io_pos;
  io_pos = 0;  // clear before recovery (create_new_log_file writes header)
  res = recover_file();
  if (res != FR_OK) return handle_error(res, "recovery");
  // Retry writing the lost buffer into the new file
  res = f_write(&log_file_obj, io_buf, saved_pos, &bw);
  if (res != FR_OK) return handle_error(res, "write");
  return FR_OK;
}

static FRESULT create_new_log_file(void) {
  DateTime dt;
  get_current_datetime(&dt);
  snprintf(log_file_name, sizeof(log_file_name), "%02d%02d%02d%02d.MLG",
           dt.day % 100, dt.hour % 100, dt.minute % 100,
           (int)(file_counter++ % 100));

  FRESULT res = f_open(&log_file_obj, log_file_name, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return handle_error(res, "create");

  // Pre-allocate contiguous space before any writes (objsize must be 0)
  f_expand(&log_file_obj, MAX_FILE_SIZE, 1);
  f_lseek(&log_file_obj, 0);

  res = write_mlg_file_header();
  if (res != FR_OK) {
    f_close(&log_file_obj);
    return handle_error(res, "header");
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

static uint32_t fault_counter = 0;

static void write_fault_file(FRESULT fault_res, const char* fault_at) {
  f_close(&log_file_obj);
  f_mount(0, "", 0);
  HAL_Delay(500);
  FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK) return;

  char fname[13];
  snprintf(fname, sizeof(fname), "FAULT_%02lu.TXT",
           (unsigned long)(fault_counter++ % 100));

  FIL fault_file;
  res = f_open(&fault_file, fname, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return;

  sd_ErrorCounters ec;
  sd_get_error_counters(&ec);

  DateTime dt;
  get_current_datetime(&dt);
  uint32_t uptime = HAL_GetTick();

  int len = snprintf((char*)io_buf, IO_BUF_SIZE,
    "=== CANLOGGER FAULT REPORT ===\r\n"
    "Time: 20%02u-%02u-%02u %02u:%02u:%02u\r\n"
    "Uptime: %lu ms (%lu s)\r\n"
    "\r\n"
    "--- Fault Info ---\r\n"
    "FatFS error: FR_%d\r\n"
    "Location: %s\r\n"
    "Error count: %d (max %d)\r\n"
    "Recovery count: %lu\r\n"
    "File: %s\r\n"
    "Files created: %lu\r\n"
    "\r\n"
    "--- SD/SDIO Error Counters ---\r\n"
    "Total callbacks: %lu\r\n"
    "CMD_RSP_TIMEOUT: %lu\r\n"
    "CMD_CRC_FAIL:    %lu\r\n"
    "DATA_TIMEOUT:    %lu\r\n"
    "DATA_CRC_FAIL:   %lu\r\n"
    "TX_UNDERRUN:     %lu\r\n"
    "DMA_ERROR:       %lu\r\n"
    "Other:           %lu\r\n"
    "Last ErrorCode:  0x%08lX\r\n"
    "HAL ErrorCode:   0x%08lX\r\n"
    "\r\n"
    "=== END FAULT REPORT ===\r\n",
    dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
    (unsigned long)uptime, (unsigned long)(uptime / 1000),
    (int)fault_res, fault_at,
    error_count, MAX_ERROR_COUNT,
    (unsigned long)recovery_count,
    log_file_name, (unsigned long)file_counter,
    (unsigned long)ec.total_callbacks,
    (unsigned long)ec.cmd_rsp_timeout,
    (unsigned long)ec.cmd_crc_fail,
    (unsigned long)ec.data_timeout,
    (unsigned long)ec.data_crc_fail,
    (unsigned long)ec.tx_underrun,
    (unsigned long)ec.dma_error,
    (unsigned long)ec.other_error,
    (unsigned long)ec.last_error_code,
    (unsigned long)ec.hal_error_code);

  if (len > 0) {
    UINT bw;
    f_write(&fault_file, io_buf, (UINT)len, &bw);
  }
  f_close(&fault_file);
}

void lw_write_test_fault(void) {
  set_error_state(FR_INT_ERR, "test");
}

static void set_error_state(FRESULT res, const char* at) {
  error_state = 1;
  last_error = res;
  last_error_at = at;
  write_fault_file(res, at);
}

static FRESULT handle_error(FRESULT res, const char* at) {
  error_count++;
  last_error = res;
  last_error_at = at;
  if (error_count >= MAX_ERROR_COUNT) set_error_state(res, at);
  return res;
}
