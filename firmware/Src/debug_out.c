#include "debug_out.h"
#include "usbd_cdc_if.h"
#include "stm32f4xx_hal.h"
#include "fatfs.h"
#include <stdio.h>
#include <string.h>

#define CDC_TX_BUF_SIZE 640
static uint8_t cdc_tx_buf[CDC_TX_BUF_SIZE];
static uint16_t cdc_tx_pos = 0;
static uint32_t last_tick = 0;
static uint8_t stream_enabled = 0;

// --- Command RX buffer ---
#define CMD_BUF_SIZE 80
static char cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_pos = 0;
static volatile uint8_t cmd_ready = 0;

// Flush buffer over USB CDC.
// wait=1: retry up to 50ms (for command responses). wait=0: drop if busy (for stream).
static uint8_t cdc_wait = 0;

static void cdc_flush(void) {
  if (cdc_tx_pos > 0) {
    if (cdc_wait) {
      uint8_t retries = 50;
      while (CDC_Transmit_FS(cdc_tx_buf, cdc_tx_pos) == USBD_BUSY && retries-- > 0) {
        HAL_Delay(1);
      }
    } else {
      CDC_Transmit_FS(cdc_tx_buf, cdc_tx_pos);
    }
    cdc_tx_pos = 0;
  }
}

int __io_putchar(int ch) {
  if (cdc_tx_pos >= CDC_TX_BUF_SIZE) {
    cdc_flush();
  }
  cdc_tx_buf[cdc_tx_pos++] = (uint8_t)ch;
  if (ch == '\n') {
    cdc_flush();
  }
  return ch;
}

// --- Universal CAN frame capture ---
#define CAN_SNIFF_MAX 16
#define CAN_SNIFF_TIMEOUT_MS 2000

typedef struct {
  uint32_t id;
  uint8_t data[8];
  uint8_t dlc;
  uint32_t last_seen;  // HAL_GetTick() when last updated
} can_sniff_entry_t;

static can_sniff_entry_t can_sniff[CAN_SNIFF_MAX];
static uint8_t can_sniff_count = 0;

void debug_out_set_can(uint32_t id, const uint8_t* data, uint8_t dlc) {
  uint32_t now = HAL_GetTick();
  for (uint8_t i = 0; i < can_sniff_count; i++) {
    if (can_sniff[i].id == id) {
      uint8_t len = dlc < 8 ? dlc : 8;
      for (uint8_t j = 0; j < len; j++) can_sniff[i].data[j] = data[j];
      can_sniff[i].dlc = dlc;
      can_sniff[i].last_seen = now;
      return;
    }
  }
  if (can_sniff_count < CAN_SNIFF_MAX) {
    can_sniff_entry_t *e = &can_sniff[can_sniff_count++];
    e->id = id;
    e->dlc = dlc < 8 ? dlc : 8;
    e->last_seen = now;
    for (uint8_t j = 0; j < e->dlc; j++) e->data[j] = data[j];
  }
}

// Saved from debug_out_tick args for use by status command
static uint32_t saved_frames = 0;
static uint16_t saved_num_fields = 0;
static int saved_init_ok = 0;

void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok) {
  saved_frames = frames_processed;
  saved_num_fields = num_fields;
  saved_init_ok = init_ok;

  uint32_t now = HAL_GetTick();
  if (stream_enabled && now - last_tick >= 1000) {
    printf("[%lu] frames=%lu fields=%u init=%d ids=%u\r\n",
           now / 1000, frames_processed, num_fields, init_ok, can_sniff_count);
    for (uint8_t i = 0; i < can_sniff_count; i++) {
      can_sniff_entry_t *e = &can_sniff[i];
      if (now - e->last_seen > CAN_SNIFF_TIMEOUT_MS) continue;
      printf("  0x%03lX[%u]:", e->id, e->dlc);
      for (uint8_t j = 0; j < e->dlc && j < 8; j++)
        printf(" %02X", e->data[j]);
      printf("\r\n");
    }
    last_tick = now;
  }
}

// --- Command receive (called from USB CDC ISR context) ---

// Static echo buffer — CDC_Transmit_FS sends asynchronously by pointer,
// so the buffer must outlive the call (no stack buffers).
static uint8_t echo_buf[CMD_BUF_SIZE + 2];

void debug_cmd_receive(const uint8_t* buf, uint32_t len) {
  // Any input stops stream so user can type
  if (stream_enabled) stream_enabled = 0;

  uint8_t echo_pos = 0;

  for (uint32_t i = 0; i < len; i++) {
    char c = (char)buf[i];
    if (c == '\r' || c == '\n') {
      if (cmd_pos > 0) {
        cmd_buf[cmd_pos] = '\0';
        cmd_ready = 1;
        echo_buf[echo_pos++] = '\r';
        echo_buf[echo_pos++] = '\n';
      }
    } else if (cmd_pos < CMD_BUF_SIZE - 1) {
      cmd_buf[cmd_pos++] = c;
      echo_buf[echo_pos++] = (uint8_t)c;
    }
  }
  // Echo all at once — single CDC_Transmit avoids per-char BUSY drops
  if (echo_pos > 0) {
    CDC_Transmit_FS(echo_buf, echo_pos);
  }
}

// --- Command handlers ---

static void cmd_help(void) {
  printf("Commands:\r\n"
         "  help    - this message\r\n"
         "  status  - system status\r\n"
         "  stream  - toggle periodic output\r\n"
         "  config  - show loaded config\r\n"
         "  ls      - list MLG files on SD\r\n"
         "  get <f> - download file (use usb_get.py)\r\n");
}

static void cmd_status(const ring_Buffer* rb) {
  uint32_t now = HAL_GetTick();
  printf("uptime=%lus frames=%lu fields=%u init=%d stream=%d\r\n",
         now / 1000, saved_frames, saved_num_fields, saved_init_ok,
         stream_enabled);

  // Log writer state
  lw_Status lws;
  lw_get_status(&lws);
  printf("file=%s size=%lu files=%lu blocks=%u err=%d/%d\r\n",
         lws.file_name, lws.file_size, lws.file_count,
         lws.block_count, lws.error_count, lws.error_state);

  // Ring buffer
  printf("rb: count=%lu head=%lu tail=%lu\r\n",
         ring_buf_count(rb), rb->head, rb->tail);

  // SD/FatFS info
  FATFS* fs;
  DWORD free_clust;
  if (f_getfree("", &free_clust, &fs) == FR_OK) {
    uint32_t total_kb = (uint32_t)((fs->n_fatent - 2) * fs->csize) / 2;
    uint32_t free_kb  = (uint32_t)(free_clust * fs->csize) / 2;
    printf("sd: %luKB free / %luKB total\r\n", free_kb, total_kb);
  } else {
    printf("sd: error reading\r\n");
  }

  // CAN IDs seen
  printf("can: %u ids\r\n", can_sniff_count);
  for (uint8_t i = 0; i < can_sniff_count; i++) {
    can_sniff_entry_t *e = &can_sniff[i];
    uint32_t age = now - e->last_seen;
    printf("  0x%03lX[%u] %lums ago:", e->id, e->dlc, age);
    for (uint8_t j = 0; j < e->dlc && j < 8; j++)
      printf(" %02X", e->data[j]);
    printf("\r\n");
  }
}

static void cmd_stream(void) {
  stream_enabled = !stream_enabled;
  printf("stream %s\r\n", stream_enabled ? "ON" : "OFF");
}

static void cmd_config(const cfg_Config* cfg) {
  if (!cfg) { printf("no config\r\n"); return; }
  printf("bitrate=%lu interval=%lums fields=%u can_ids=%u\r\n",
         cfg->can_bitrate, cfg->log_interval_ms,
         cfg->num_fields, cfg->num_can_ids);
  for (uint16_t i = 0; i < cfg->num_fields; i++) {
    const cfg_Field* f = &cfg->fields[i];
    int s1000 = (int)(f->scale * 1000);
    int o100 = (int)(f->offset * 100);
    int s_neg = s1000 < 0; if (s_neg) s1000 = -s1000;
    int o_neg = o100 < 0;  if (o_neg) o100 = -o100;
    printf("  [%u] 0x%03lX b%u:%u %s (%s) s=%s%d.%03d o=%s%d.%02d",
           i, f->can_id, f->start_byte, f->bit_length,
           f->name, f->units,
           s_neg ? "-" : "", s1000 / 1000, s1000 % 1000,
           o_neg ? "-" : "", o100 / 100, o100 % 100);
    if (f->lut_count > 0) printf(" lut=%u", f->lut_count);
    printf("\r\n");
  }
}

static void cmd_ls(void) {
  DIR dir;
  FILINFO fno;
  FRESULT res = f_opendir(&dir, "/");
  if (res != FR_OK) {
    printf("SD error %d\r\n", res);
    return;
  }
  uint16_t count = 0;
  while (1) {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0) break;
    if (fno.fattrib & AM_DIR) continue;
    // Show all files, highlight MLG
    printf("  %-12s %7lu\r\n", fno.fname, fno.fsize);
    count++;
  }
  f_closedir(&dir);
  printf("%u files\r\n", count);
}

static void cmd_get(const char* filename) {
  FIL file;
  FRESULT res = f_open(&file, filename, FA_READ);
  if (res != FR_OK) {
    printf("ERR:open %d\r\n", res);
    return;
  }

  uint32_t size = f_size(&file);
  // Pause stream during transfer
  uint8_t was_streaming = stream_enabled;
  stream_enabled = 0;

  printf("FILE:%s:%lu\n", filename, size);

  uint8_t fbuf[512];
  UINT br;
  while (size > 0) {
    res = f_read(&file, fbuf, sizeof(fbuf), &br);
    if (res != FR_OK || br == 0) break;
    // Send raw bytes via CDC, wait if busy
    uint8_t retries = 200;
    while (CDC_Transmit_FS(fbuf, br) == USBD_BUSY && retries-- > 0) {
      HAL_Delay(1);
    }
    size -= br;
  }
  f_close(&file);

  // Small delay to let last chunk transmit before sending END marker
  HAL_Delay(5);
  printf("\nEND\n");

  stream_enabled = was_streaming;
}

// --- Command poll (called from main loop) ---

void debug_cmd_poll(const cfg_Config* cfg, int init_ok, const ring_Buffer* rb) {
  if (!cmd_ready) return;

  // Copy and reset
  char line[CMD_BUF_SIZE];
  memcpy(line, cmd_buf, cmd_pos + 1);
  cmd_pos = 0;
  cmd_ready = 0;

  // Trim trailing spaces
  size_t len = strlen(line);
  while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';

  // Enable reliable flush for command responses
  cdc_wait = 1;

  // Parse command
  if (strcmp(line, "help") == 0) {
    cmd_help();
  } else if (strcmp(line, "status") == 0) {
    cmd_status(rb);
  } else if (strcmp(line, "stream") == 0) {
    cmd_stream();
  } else if (strcmp(line, "config") == 0) {
    cmd_config(init_ok ? cfg : NULL);
  } else if (strcmp(line, "ls") == 0) {
    cmd_ls();
  } else if (strncmp(line, "get ", 4) == 0) {
    cmd_get(line + 4);
  } else {
    printf("unknown: %s\r\n", line);
  }

  cdc_wait = 0;
}
