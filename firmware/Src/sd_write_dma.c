/*
 * Override __weak BSP_SD_WriteBlocks_DMA with ChibiOS-style operation order.
 * Stock HAL sends CMD24 before DMA start — card begins waiting for data while
 * FIFO is empty → TX_UNDERRUN. We start DMA first so FIFO fills before the
 * card expects data.
 *
 * Order: DMA_Start_IT → CMD24/CMD25 → DCTRL (DMAEN+DTEN)
 * See: ChibiOS SDIOv1 hal_sdc_lld.c
 *
 * This file overrides the __weak BSP_SD_WriteBlocks_DMA in bsp_driver_sd.c
 * via the linker's weak symbol resolution. Kept separate so CubeMX codegen
 * never touches it.
 */

#include "bsp_driver_sd.h"
#include "stm32f4xx_ll_sdmmc.h"
#include "sd_write_dma.h"

extern SD_HandleTypeDef hsd;

/* --- SDIO error counters (incremented from ISR) --- */

static volatile sd_ErrorCounters sd_err_counters;

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  uint32_t err = hsd->ErrorCode;
  sd_err_counters.total_callbacks++;
  sd_err_counters.last_error_code = err;

  if (err & HAL_SD_ERROR_CMD_RSP_TIMEOUT) sd_err_counters.cmd_rsp_timeout++;
  if (err & HAL_SD_ERROR_CMD_CRC_FAIL)    sd_err_counters.cmd_crc_fail++;
  if (err & HAL_SD_ERROR_DATA_TIMEOUT)     sd_err_counters.data_timeout++;
  if (err & HAL_SD_ERROR_DATA_CRC_FAIL)    sd_err_counters.data_crc_fail++;
  if (err & HAL_SD_ERROR_TX_UNDERRUN)      sd_err_counters.tx_underrun++;
  if (err & HAL_SD_ERROR_DMA)              sd_err_counters.dma_error++;

  uint32_t known = HAL_SD_ERROR_CMD_RSP_TIMEOUT | HAL_SD_ERROR_CMD_CRC_FAIL |
                   HAL_SD_ERROR_DATA_TIMEOUT | HAL_SD_ERROR_DATA_CRC_FAIL |
                   HAL_SD_ERROR_TX_UNDERRUN | HAL_SD_ERROR_DMA;
  if (err & ~known) sd_err_counters.other_error++;
}

void sd_get_error_counters(sd_ErrorCounters* out)
{
  out->cmd_rsp_timeout = sd_err_counters.cmd_rsp_timeout;
  out->cmd_crc_fail    = sd_err_counters.cmd_crc_fail;
  out->data_timeout    = sd_err_counters.data_timeout;
  out->data_crc_fail   = sd_err_counters.data_crc_fail;
  out->tx_underrun     = sd_err_counters.tx_underrun;
  out->dma_error       = sd_err_counters.dma_error;
  out->other_error     = sd_err_counters.other_error;
  out->total_callbacks = sd_err_counters.total_callbacks;
  out->last_error_code = sd_err_counters.last_error_code;
  out->hal_error_code  = hsd.ErrorCode;
}

/* Local copies of HAL-internal DMA callbacks (static in hal_sd.c) */
static void SD_DMATransmitCplt_Fixed(DMA_HandleTypeDef *hdma)
{
  SD_HandleTypeDef* p = (SD_HandleTypeDef*)(hdma->Parent);
  __HAL_SD_ENABLE_IT(p, (SDIO_IT_DATAEND));
}

static void SD_DMAError_Fixed(DMA_HandleTypeDef *hdma)
{
  SD_HandleTypeDef* p = (SD_HandleTypeDef*)(hdma->Parent);
  if (HAL_DMA_GetError(hdma) != HAL_DMA_ERROR_FE) {
    p->ErrorCode |= HAL_SD_ERROR_DMA;
    __HAL_SD_CLEAR_FLAG(p, SDIO_STATIC_FLAGS);
    p->Instance->DCTRL &= ~SDIO_DCTRL_DMAEN;
    p->State = HAL_SD_STATE_READY;
    p->Context = SD_CONTEXT_NONE;
    HAL_SD_ErrorCallback(p);
  }
}

uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
  uint32_t errorstate;
  uint32_t add = WriteAddr;

  if (hsd.State != HAL_SD_STATE_READY) return MSD_ERROR;

  hsd.ErrorCode = HAL_SD_ERROR_NONE;
  if ((add + NumOfBlocks) > hsd.SdCard.LogBlockNbr) return MSD_ERROR;

  hsd.State = HAL_SD_STATE_BUSY;
  hsd.Instance->DCTRL = 0U;

  __HAL_SD_ENABLE_IT(&hsd, (SDIO_IT_DCRCFAIL | SDIO_IT_DTIMEOUT | SDIO_IT_TXUNDERR | SDIO_IT_STBITERR));

  hsd.hdmatx->XferCpltCallback = SD_DMATransmitCplt_Fixed;
  hsd.hdmatx->XferErrorCallback = SD_DMAError_Fixed;
  hsd.hdmatx->XferAbortCallback = NULL;

  if (NumOfBlocks > 1U)
    hsd.Context = (SD_CONTEXT_WRITE_MULTIPLE_BLOCK | SD_CONTEXT_DMA);
  else
    hsd.Context = (SD_CONTEXT_WRITE_SINGLE_BLOCK | SD_CONTEXT_DMA);

  if (hsd.SdCard.CardType != CARD_SDHC_SDXC) add *= 512U;

  /* Force DMA direction */
  hsd.hdmatx->Init.Direction = DMA_MEMORY_TO_PERIPH;
  MODIFY_REG(hsd.hdmatx->Instance->CR, DMA_SxCR_DIR, DMA_MEMORY_TO_PERIPH);

  /* Step 1: Start DMA FIRST — FIFO begins filling */
  if (HAL_DMA_Start_IT(hsd.hdmatx, (uint32_t)pData, (uint32_t)&hsd.Instance->FIFO,
                       (uint32_t)(BLOCKSIZE * NumOfBlocks) / 4U) != HAL_OK)
  {
    __HAL_SD_DISABLE_IT(&hsd, (SDIO_IT_DCRCFAIL | SDIO_IT_DTIMEOUT | SDIO_IT_TXUNDERR | SDIO_IT_STBITERR));
    hsd.ErrorCode |= HAL_SD_ERROR_DMA;
    hsd.State = HAL_SD_STATE_READY;
    hsd.Context = SD_CONTEXT_NONE;
    return MSD_ERROR;
  }

  /* Step 2: Send write command AFTER DMA is running */
  if (NumOfBlocks > 1U)
    errorstate = SDMMC_CmdWriteMultiBlock(hsd.Instance, add);
  else
    errorstate = SDMMC_CmdWriteSingleBlock(hsd.Instance, add);

  if (errorstate != HAL_SD_ERROR_NONE)
  {
    HAL_DMA_Abort(hsd.hdmatx);
    __HAL_SD_CLEAR_FLAG(&hsd, SDIO_STATIC_FLAGS);
    hsd.ErrorCode |= errorstate;
    hsd.State = HAL_SD_STATE_READY;
    hsd.Context = SD_CONTEXT_NONE;
    return MSD_ERROR;
  }

  /* Step 3: Enable SDIO data path LAST */
  hsd.Instance->DTIMER = SDMMC_DATATIMEOUT;
  hsd.Instance->DLEN   = BLOCKSIZE * NumOfBlocks;
  hsd.Instance->DCTRL  = SDIO_DCTRL_DMAEN | SDIO_DCTRL_DTEN | SDIO_DATABLOCK_SIZE_512B;

  return MSD_OK;
}
