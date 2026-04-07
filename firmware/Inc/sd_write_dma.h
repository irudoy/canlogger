#ifndef SD_WRITE_DMA_H
#define SD_WRITE_DMA_H

#include <stdint.h>

typedef struct {
  uint32_t cmd_rsp_timeout;
  uint32_t cmd_crc_fail;
  uint32_t data_timeout;
  uint32_t data_crc_fail;
  uint32_t tx_underrun;
  uint32_t dma_error;
  uint32_t other_error;
  uint32_t total_callbacks;
  uint32_t last_error_code;  // hsd->ErrorCode snapshot from last callback
  uint32_t hal_error_code;   // current hsd.ErrorCode (includes CMD13 polling errors)
} sd_ErrorCounters;

void sd_get_error_counters(sd_ErrorCounters* out);

#endif
