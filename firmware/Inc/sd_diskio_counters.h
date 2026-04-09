#ifndef SD_DISKIO_COUNTERS_H
#define SD_DISKIO_COUNTERS_H

#include <stdint.h>

// Fine-grained instrumentation of SD_write() in sd_diskio.c.
// Populated by a replacement SD_write installed via USER CODE sections
// (rename trick: #define SD_write SD_write_deprecated + new definition
//  in lastSection). Lets us pinpoint which of SD_write's 4 failure points
//  is responsible for FR_DISK_ERR during stress logging.
typedef struct {
  uint32_t total_writes;           // every call to SD_write
  uint32_t used_scratch_path;      // unaligned buff -> slow path taken
  // Failure points (fast, aligned path):
  uint32_t err_enter_busy;         // SD_CheckStatusWithTimeout failed at entry
  uint32_t err_dma_start;          // BSP_SD_WriteBlocks_DMA != MSD_OK
  uint32_t err_tx_cplt_timeout;    // WriteStatus never set (callback missed)
  uint32_t err_cardstate_timeout;  // BSP_SD_GetCardState never TRANSFER_OK
  // Failure points (scratch, slow path):
  uint32_t err_slow_dma_start;     // BSP_SD_WriteBlocks_DMA on scratch failed
  uint32_t err_slow_tx_cplt;       // scratch path WriteStatus timeout
  // Diagnostics for last failure:
  uint32_t last_err_sector;
  uint32_t last_err_count;
  uint32_t last_err_tick;
  uint32_t max_latency_ms;         // longest single SD_write call so far
  uint32_t last_latency_ms;        // latency of the most recent call

  // SD_status instrumentation — catches the flaky-CMD13 path that
  // makes f_sync's validate() return FR_INVALID_OBJECT without a
  // single SD_write being attempted.
  uint32_t status_calls;           // total SD_status invocations
  uint32_t status_fail_not_ready;  // BSP_SD_GetCardState() != MSD_OK on first poll
  uint32_t status_retry_rescued;   // first poll failed but retry loop recovered
  uint32_t status_hard_fail;       // retry loop exhausted SD_STATUS_RETRY_MS
  uint32_t status_max_retry_ms;    // longest retry wait that eventually succeeded
  uint32_t last_card_state_raw;    // raw HAL_SD card state on last hard failure
} sd_sdio_Counters;

void sd_sdio_get_counters(sd_sdio_Counters* out);

#endif // SD_DISKIO_COUNTERS_H
