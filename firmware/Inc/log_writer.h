#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include "fatfs.h"
#include "config.h"
#include "can_map.h"

// Initialize: mount SD, read config, write MLG header.
// Returns 0 on success, -1 on error (check lw_get_error).
int lw_init(cfg_Config* cfg_out, can_FieldValues* fv_out);

// Write a data block from shadow buffer if log interval elapsed.
FRESULT lw_tick(const can_FieldValues* fv, uint32_t log_interval_ms);

// Flush and close.
void lw_stop(void);

// Pause logging: flush, close log file, keep SD mounted.
// Used during CDC file upload to avoid interleaving writes on same card.
// Logger does not resume automatically — expected to be followed by reboot.
void lw_pause(void);

// LED update (call every loop iteration).
void lw_update_leds(void);

// Check error state.
int lw_is_error(void);

// Debug status info.
typedef struct {
  const char* file_name;   // current log file name
  uint32_t    file_size;   // current log file size
  uint32_t    file_count;  // number of log files created
  int         error_count; // SD error count
  int         error_state; // 1 = fatal error
  FRESULT     last_error;  // last FatFS error code
  const char* last_error_at; // where last error occurred (e.g. "mount", "write")
  FRESULT     last_rec_res;  // FRESULT that triggered most recent recovery
  const char* last_rec_at;   // where last recovery was triggered from ("sync"|"write")
  uint32_t    recovery_count; // successful write retries (GC stall recoveries)
  uint8_t     block_count; // data blocks written in current file
  // SD/SDIO error counters (from HAL_SD_ErrorCallback ISR)
  uint32_t    sd_cmd_timeout;   // CMD_RSP_TIMEOUT count
  uint32_t    sd_data_timeout;  // DATA_TIMEOUT count
  uint32_t    sd_data_crc_fail; // DATA_CRC_FAIL count
  uint32_t    sd_dma_error;     // DMA error count
  uint32_t    sd_err_callbacks; // total error callbacks
  uint32_t    sd_last_err_code; // raw hsd->ErrorCode from last callback
  uint32_t    sd_hal_err_code;  // current hsd.ErrorCode (includes CMD13 polling errors)
} lw_Status;

void lw_get_status(lw_Status* out);

// Write a test FAULT file to SD (for diagnostics).
void lw_write_test_fault(void);

#endif // LOG_WRITER_H
