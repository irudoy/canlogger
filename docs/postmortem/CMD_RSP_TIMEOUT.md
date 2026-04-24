# CMD_RSP_TIMEOUT during DMA writes to SD

## Symptom

Occasional `CMD_RSP_TIMEOUT` (`SDIO_STA[2] CTIMEOUT`) on DMA writes. Not fatal — the write still completes successfully.

## What CTIMEOUT is

A hardware flag of the STM32F407 SDIO peripheral. It latches when the CPSM (Command Path State Machine) does not receive a response from the SD card within 64 SDIO_CK cycles (~2.7 µs at 24 MHz).

- Register: `SDIO_STA`, bit 2 (`SDIO_FLAG_CTIMEOUT`)
- HAL constant: `HAL_SD_ERROR_CMD_RSP_TIMEOUT` = `SDMMC_ERROR_CMD_RSP_TIMEOUT`
- Definition: `stm32f4xx_ll_sdmmc.h:325` — `SDIO_CMDTIMEOUT = 5000` (software timeout on top of the hardware one)

## Three places where it can happen

### 1. CMD12 (STOP_TRANSMISSION) — most likely

**Where:** `HAL_SD_IRQHandler` → line 1584 of `stm32f4xx_hal_sd.c`

**When:** After the DMA multi-block write completes (DATAEND interrupt). HAL issues CMD12 to terminate the multi-block operation. If the card is busy programming flash it does not answer CMD12 within 64 SDIO_CK cycles.

**Why it is not fatal:** after CMD12 (even if it failed) the flow continues:
1. The error is OR'd into `hsd->ErrorCode`
2. `HAL_SD_ErrorCallback` fires
3. State still transitions to READY, and `HAL_SD_TxCpltCallback` is called
4. `WriteStatus = 1` is set in `BSP_SD_WriteCpltCallback`
5. `sd_diskio.c` sees WriteStatus=1 and starts polling card state with CMD13
6. The card eventually responds → `RES_OK`

**Risk:** without a successful CMD12 the card could stay in RCV state, expecting more data. In practice DATAEND means all data has been transferred, and the card knows that from the SDIO data path being disabled.

CMD12 timeout: `SDIO_STOPTRANSFERTIMEOUT = 100_000_000` polling-loop iterations — about 4.7 s at 168 MHz. But the hardware CTIMEOUT fires much earlier.

### 2. CMD13 (SEND_STATUS) — while polling card state

**Where:** `SD_CheckStatusWithTimeout` → `BSP_SD_GetCardState` → `HAL_SD_GetCardState` → `SD_SendStatus` → `SDMMC_CmdSendStatus`

**When:**
- Before every write (`sd_diskio.c:325`)
- After every write while waiting for the card to leave the Programming state (`sd_diskio.c:364`)

**Behaviour:** if CMD13 fails:
- `HAL_SD_GetCardState` returns 0 (resp1 is not populated)
- `BSP_SD_GetCardState` returns `SD_TRANSFER_BUSY` (0 ≠ HAL_SD_CARD_TRANSFER=4)
- Polling continues — this is correct
- But `hsd->ErrorCode` is polluted by the OR of the error bits

### 3. CMD24/CMD25 (WRITE_SINGLE/MULTI_BLOCK) — at the start of a write

**Where:** `BSP_SD_WriteBlocks_DMA` (`sd_write_dma.c:83-85`)

**When:** if the card is still busy from the previous operation and `SD_CheckStatusWithTimeout` missed it (unlikely with the 30 s timeout).

**Behaviour:** fatal for that write — DMA abort, `MSD_ERROR` → `RES_ERROR` → `FR_DISK_ERR`.

## Call chain on a DMA write

```
main loop → lw_tick → flush_io_buf → f_write
  → sd_diskio SD_write
    → SD_CheckStatusWithTimeout(30s)     ← CMD13 polling, CTIMEOUT = BUSY
    → BSP_SD_WriteBlocks_DMA             ← sd_write_dma.c
      → HAL_DMA_Start_IT                 ← DMA start
      → SDMMC_CmdWriteMultiBlock         ← CMD25, CTIMEOUT = fatal for the write
      → DCTRL enable                     ← data flows
    ← returns MSD_OK

    [ISR context]
    DMA complete → SD_DMATransmitCplt_Fixed → enable SDIO_IT_DATAEND
    SDIO DATAEND IRQ → HAL_SD_IRQHandler
      → SDMMC_CmdStopTransfer            ← CMD12, CTIMEOUT = non-fatal
      → HAL_SD_TxCpltCallback → BSP_SD_WriteCpltCallback → WriteStatus=1

    [back in SD_write]
    → wait WriteStatus == 1 (30s)
    → BSP_SD_GetCardState loop (30s)     ← CMD13 polling, CTIMEOUT = BUSY
    ← RES_OK
```

## Relation to SD GC stalls

The SD card runs internal Garbage Collection (moving data between flash blocks) periodically. During GC:
- DAT0 is held low (busy)
- The card may not answer commands (CTIMEOUT)
- GC duration: from a few ms to hundreds of ms

This is documented in `SD_ERRORS.md` as the main cause of write failures.

## STM32F4 SDIO errata (ES0182)

1. **Hardware Flow Control** — glitches when HWFC_EN=1, do not use
2. **NEGEDGE bit** — incorrect sampling when NEGEDGE=1
3. **BYPASS + NEGEDGE** — hold timing violated

None of these bits are set in our project — the errata do not apply.

## Status

All monitoring measures are in place:

- `HAL_SD_ErrorCallback` override with per-flag counters — `firmware/Src/sd_write_dma.c`
- Counters in the CDC `status` output (`CMD_RSP_TIMEOUT: N` etc.) — `firmware/Src/log_writer.c`
- FAULT_NN.TXT on SD on a fatal error — `firmware/Src/debug_out.c`

On the production workload CMD_RSP_TIMEOUT is observed rarely and does not escalate into recovery (ErrorCode is cleared before the next operation). This document is kept as a reference on the nature of the error; no further action required.
