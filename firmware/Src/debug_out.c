#include "debug_out.h"
#include "usbd_cdc_if.h"
#include "sd_write_dma.h"
#include "sd_diskio_counters.h"
#include "stm32f4xx_hal.h"
#include "fatfs.h"
#include <stdio.h>
#include <string.h>

// CubeMX-generated USB CDC RX buffer — used to re-arm reception after flush
extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
extern USBD_HandleTypeDef hUsbDeviceFS;

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

// --- File upload state (streaming) ---
// put_buf is a staging area filled from USB ISR, drained by main loop to SD.
// Flow control: when buf near-full, USB reception is stalled (not re-armed)
// so host naturally NAKs and waits. Main loop re-arms after SD flush.
#define PUT_BUF_SIZE 4096
static uint8_t put_buf[PUT_BUF_SIZE] __attribute__((aligned(4)));
static volatile uint32_t put_head = 0;       // write position in put_buf (ISR)
static volatile uint32_t put_received = 0;   // total bytes received from USB
static volatile uint32_t put_expected = 0;   // total bytes expected
static volatile uint8_t put_active = 0;      // 1 = receiving file data
volatile uint8_t put_stall = 0;              // 1 = USB RX stalled, main must re-arm
static uint8_t put_err = 0;                  // sticky error during streaming
static char put_filename[13];                // 8.3 filename
static FIL put_file;
static uint8_t put_file_open = 0;

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
  // File upload raw mode — no echo, no command parsing.
  // Copy bytes into put_buf. If buf fills or we received all expected bytes,
  // set put_stall=1 — CDC_Receive_FS will NOT re-arm, host stalls (NAK).
  // Main loop drains put_buf to SD and re-arms reception.
  if (put_active) {
    uint32_t space = PUT_BUF_SIZE - put_head;
    uint32_t remaining = put_expected - put_received;
    uint32_t to_copy = len;
    if (to_copy > space) to_copy = space;
    if (to_copy > remaining) to_copy = remaining;

    if (to_copy > 0) {
      memcpy(put_buf + put_head, buf, to_copy);
      put_head += to_copy;
      put_received += to_copy;
    }

    // Stall if buf can't hold next 64-byte USB packet, or done receiving.
    if (put_head + 64 > PUT_BUF_SIZE || put_received >= put_expected) {
      put_stall = 1;
    }
    return;
  }

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
         "  help      - this message\r\n"
         "  status    - system status\r\n"
         "  stream    - toggle periodic output\r\n"
         "  config    - show loaded config\r\n"
         "  ls        - list MLG files on SD\r\n"
         "  get <f>   - download file (use usb_get.py)\r\n"
         "  put <f> N - upload N bytes to file\r\n"
         "  fault     - simulate fatal error, write FAULT file\r\n"
         "  stop      - close SD safely (before flash)\r\n");
}

static void cmd_status(const ring_Buffer* rb) {
  uint32_t now = HAL_GetTick();
  printf("uptime=%lus frames=%lu fields=%u init=%d stream=%d\r\n",
         now / 1000, saved_frames, saved_num_fields, saved_init_ok,
         stream_enabled);

  // Log writer state
  lw_Status lws;
  lw_get_status(&lws);
  printf("file=%s size=%lu files=%lu blocks=%u err=%d/%d",
         lws.file_name, lws.file_size, lws.file_count,
         lws.block_count, lws.error_count, lws.error_state);
  if (lws.error_count > 0 || lws.error_state) {
    printf(" last=FR_%d@%s", (int)lws.last_error, lws.last_error_at);
  }
  if (lws.recovery_count > 0) {
    printf(" rec=%lu lastrec=FR_%d@%s",
           lws.recovery_count,
           (int)lws.last_rec_res, lws.last_rec_at);
  }
  printf("\r\n");

  if (lws.sd_err_callbacks > 0 || lws.sd_hal_err_code != 0) {
    printf("sdio: cb=%lu ctmo=%lu dtmo=%lu dcrc=%lu dma=%lu last=0x%08lX hal=0x%08lX\r\n",
           lws.sd_err_callbacks, lws.sd_cmd_timeout,
           lws.sd_data_timeout, lws.sd_data_crc_fail,
           lws.sd_dma_error, lws.sd_last_err_code,
           lws.sd_hal_err_code);
  }

  // Fine-grained early-return counters in BSP_SD_WriteBlocks_DMA
  sd_ErrorCounters ec;
  sd_get_error_counters(&ec);
  uint32_t fgsum = ec.w_state_not_ready + ec.w_cmd13_error + ec.w_cmd13_timeout +
                   ec.w_dma_start_fail + ec.w_cmd_write_fail + ec.w_addr_oob;
  if (fgsum > 0) {
    printf("sd_w: nr=%lu c13e=%lu c13t=%lu dma=%lu cmd=%lu oob=%lu\r\n",
           ec.w_state_not_ready, ec.w_cmd13_error, ec.w_cmd13_timeout,
           ec.w_dma_start_fail, ec.w_cmd_write_fail, ec.w_addr_oob);
  }

  // SD_write() instrumentation in sd_diskio.c (pinpoints which of the
  // 4 failure points inside SD_write is firing during FR_DISK_ERR).
  sd_sdio_Counters sc;
  sd_sdio_get_counters(&sc);
  printf("sdw: tot=%lu lat=%lu/%lu scratch=%lu\r\n",
         sc.total_writes, sc.last_latency_ms, sc.max_latency_ms,
         sc.used_scratch_path);
  printf("sdst: calls=%lu fail=%lu last_raw=%lu\r\n",
         sc.status_calls, sc.status_fail_not_ready, sc.last_card_state_raw);
  uint32_t sdw_errsum = sc.err_enter_busy + sc.err_dma_start +
                        sc.err_tx_cplt_timeout + sc.err_cardstate_timeout +
                        sc.err_slow_dma_start + sc.err_slow_tx_cplt;
  if (sdw_errsum > 0) {
    printf("sdw_err: eb=%lu dma=%lu txto=%lu csto=%lu sdma=%lu stxto=%lu"
           " @sec=%lu cnt=%lu tick=%lu\r\n",
           sc.err_enter_busy, sc.err_dma_start,
           sc.err_tx_cplt_timeout, sc.err_cardstate_timeout,
           sc.err_slow_dma_start, sc.err_slow_tx_cplt,
           sc.last_err_sector, sc.last_err_count, sc.last_err_tick);
  }

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
  // Streaming file upload: flush put_buf to SD when stalled or done.
  // Since put_stall=1 causes CDC NOT to re-arm reception, ISR cannot touch
  // put_buf while we're here — so no locking needed.
  if (put_active && put_stall) {
    cdc_wait = 1;

    if (put_head > 0 && !put_err) {
      UINT bw;
      FRESULT res = f_write(&put_file, put_buf, put_head, &bw);
      if (res != FR_OK || bw != put_head) {
        put_err = 1;
        printf("ERR:write %d\r\n", res);
      }
      put_head = 0;
    }

    if (put_received >= put_expected || put_err) {
      // Done (or errored) — close file and finalize
      if (put_file_open) {
        f_close(&put_file);
        put_file_open = 0;
      }
      uint8_t success = !put_err;
      put_active = 0;
      put_stall = 0;
      if (success) {
        printf("OK %lu\r\n", (uint32_t)put_received);
        printf("Reboot to apply.\r\n");
      }
      put_err = 0;
      // Re-arm reception so CLI commands work again.
      // Briefly mask USB IRQ to avoid racing with a concurrent RX callback
      // that could touch the endpoint state mid-setup.
      HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
      USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
      USBD_CDC_ReceivePacket(&hUsbDeviceFS);
      HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
      // Logger stays paused — user will reboot to apply new config
    } else {
      // More to receive — re-arm USB reception
      put_stall = 0;
      HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
      USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
      USBD_CDC_ReceivePacket(&hUsbDeviceFS);
      HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
    cdc_wait = 0;
  }

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
  } else if (strcmp(line, "fault") == 0) {
    lw_write_test_fault();
    printf("FAULT file written\r\n");
  } else if (strcmp(line, "stop") == 0) {
    extern volatile uint8_t lw_shutdown;
    printf("Stopping SD...\r\n");
    lw_shutdown = 1;
  } else if (strncmp(line, "get ", 4) == 0) {
    cmd_get(line + 4);
  } else if (strncmp(line, "put ", 4) == 0) {
    // Parse: put FILENAME SIZE. Streaming — no upper size limit.
    char* space = strchr(line + 4, ' ');
    if (!space) {
      printf("usage: put FILENAME SIZE\r\n");
    } else {
      *space = '\0';
      const char* fname = line + 4;
      uint32_t size = 0;
      const char* sp = space + 1;
      while (*sp >= '0' && *sp <= '9') { size = size * 10 + (*sp - '0'); sp++; }

      if (size == 0) {
        printf("ERR:size\r\n");
      } else {
        // Pause logger BEFORE opening put_file. Streaming put can take
        // seconds for large files; interleaving with lw_tick writes would
        // cause FAT-table contention on the same SD → CMD_RSP_TIMEOUT.
        // lw_pause flushes & closes the log file but keeps SD mounted so
        // we can f_open put_file on the same volume. User must reboot
        // after upload to apply config anyway.
        if (init_ok) lw_pause();

        strncpy(put_filename, fname, sizeof(put_filename) - 1);
        put_filename[sizeof(put_filename) - 1] = '\0';

        FRESULT res = f_open(&put_file, put_filename, FA_CREATE_ALWAYS | FA_WRITE);
        if (res != FR_OK) {
          printf("ERR:open %d\r\n", res);
        } else {
          put_file_open = 1;
          put_expected = size;
          put_received = 0;
          put_head = 0;
          put_stall = 0;
          put_err = 0;
          put_active = 1;
          printf("READY %lu\r\n", size);
        }
      }
    }
  } else {
    printf("unknown: %s\r\n", line);
  }

  cdc_wait = 0;
}
