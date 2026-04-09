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
  // Fine-grained early-return counters in BSP_SD_WriteBlocks_DMA
  // (below SDIO ISR — not seen by HAL_SD_ErrorCallback)
  uint32_t w_state_not_ready;  // hsd.State != READY at entry
  uint32_t w_cmd13_error;      // CMD13 polling got hsd.ErrorCode
  uint32_t w_cmd13_timeout;    // CMD13 polling exceeded SDMMC_DATATIMEOUT
  uint32_t w_dma_start_fail;   // HAL_DMA_Start_IT failed
  uint32_t w_cmd_write_fail;   // SDMMC_CmdWrite{Single,Multi}Block failed
  uint32_t w_addr_oob;         // out-of-bounds address
} sd_ErrorCounters;

void sd_get_error_counters(sd_ErrorCounters* out);

#endif
