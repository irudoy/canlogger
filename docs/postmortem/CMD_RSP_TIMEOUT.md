# CMD_RSP_TIMEOUT при DMA записи на SD

## Симптом

Периодический `CMD_RSP_TIMEOUT` (`SDIO_STA[2] CTIMEOUT`) при DMA записи. Не фатальный — запись завершается успешно.

## Что такое CTIMEOUT

Аппаратный флаг SDIO периферии STM32F407. Устанавливается когда CPSM (Command Path State Machine) не получил ответ от SD-карты за 64 такта SDIO_CK (~2.7 мкс при 24 МГц).

- Регистр: `SDIO_STA`, бит 2 (`SDIO_FLAG_CTIMEOUT`)
- HAL константа: `HAL_SD_ERROR_CMD_RSP_TIMEOUT` = `SDMMC_ERROR_CMD_RSP_TIMEOUT`
- Определение: `stm32f4xx_ll_sdmmc.h:325` — `SDIO_CMDTIMEOUT = 5000` (программный таймаут поверх аппаратного)

## Три места возникновения

### 1. CMD12 (STOP_TRANSMISSION) — наиболее вероятно

**Где:** `HAL_SD_IRQHandler` → строка 1584 `stm32f4xx_hal_sd.c`

**Когда:** После завершения DMA multi-block write (DATAEND interrupt). HAL отправляет CMD12 чтобы завершить multi-block операцию. Если карта занята программированием flash — не отвечает на CMD12 за 64 SDIO_CK цикла.

**Почему не фатальный:** После CMD12 (даже если failed) flow продолжается:
1. Ошибка ORится в `hsd->ErrorCode`
2. `HAL_SD_ErrorCallback` вызывается
3. Но затем state → READY, `HAL_SD_TxCpltCallback` вызывается
4. `WriteStatus = 1` устанавливается в `BSP_SD_WriteCpltCallback`
5. `sd_diskio.c` видит WriteStatus=1, начинает polling card state через CMD13
6. Карта в конце концов отвечает → `RES_OK`

**Риск:** Без успешного CMD12 карта может остаться в RCV state, ожидая ещё данных. Но на практике DATAEND означает что все данные переданы, и карта это знает по SDIO data path disable.

Таймаут CMD12: `SDIO_STOPTRANSFERTIMEOUT = 100_000_000` итераций polling loop — это ~4.7 секунд при 168 МГц. Но аппаратный CTIMEOUT срабатывает раньше.

### 2. CMD13 (SEND_STATUS) — при polling card state

**Где:** `SD_CheckStatusWithTimeout` → `BSP_SD_GetCardState` → `HAL_SD_GetCardState` → `SD_SendStatus` → `SDMMC_CmdSendStatus`

**Когда:** 
- Перед каждой записью (`sd_diskio.c:325`)
- После каждой записи, ожидание выхода карты из Programming state (`sd_diskio.c:364`)

**Поведение:** Если CMD13 фейлит:
- `HAL_SD_GetCardState` возвращает 0 (resp1 не заполнен)
- `BSP_SD_GetCardState` возвращает `SD_TRANSFER_BUSY` (0 ≠ HAL_SD_CARD_TRANSFER=4)
- Polling продолжается — это корректно
- Но `hsd->ErrorCode` загрязняется ORом ошибок

### 3. CMD24/CMD25 (WRITE_SINGLE/MULTI_BLOCK) — при начале записи

**Где:** `BSP_SD_WriteBlocks_DMA` (`sd_write_dma.c:83-85`)

**Когда:** Если карта ещё busy от предыдущей операции и `SD_CheckStatusWithTimeout` пропустил (маловероятно при 30с таймауте).

**Поведение:** Фатальный для данной записи — DMA abort, `MSD_ERROR` → `RES_ERROR` → `FR_DISK_ERR`.

## Цепочка вызовов при DMA записи

```
main loop → lw_tick → flush_io_buf → f_write
  → sd_diskio SD_write
    → SD_CheckStatusWithTimeout(30s)     ← CMD13 polling, CTIMEOUT = BUSY
    → BSP_SD_WriteBlocks_DMA             ← sd_write_dma.c
      → HAL_DMA_Start_IT                 ← DMA запуск
      → SDMMC_CmdWriteMultiBlock         ← CMD25, CTIMEOUT = fatal для записи
      → DCTRL enable                     ← данные пошли
    ← возврат MSD_OK

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

## Связь с SD GC stalls

SD-карта выполняет внутренний Garbage Collection (перенос данных между flash-блоками) периодически. Во время GC:
- DAT0 удерживается low (busy)
- Карта может не отвечать на команды (CTIMEOUT)
- Длительность GC: от единиц мс до сотен мс

Это описано в `SD_ERRORS.md` как основная причина отказов записи.

## STM32F4 SDIO Errata (ES0182)

1. **Hardware Flow Control** — глитчи при HWFC_EN=1, не использовать
2. **NEGEDGE bit** — некорректное сэмплирование при NEGEDGE=1
3. **BYPASS + NEGEDGE** — hold timing нарушен

В нашем проекте ни один из этих битов не установлен — errata не применимы.

## Статус

Все мониторинговые меры реализованы:

- `HAL_SD_ErrorCallback` override с per-flag счётчиками — `firmware/Src/sd_write_dma.c`
- Счётчики в CDC `status` (`CMD_RSP_TIMEOUT: N` и пр.) — `firmware/Src/log_writer.c`
- FAULT_NN.TXT на SD при фатальной ошибке — `firmware/Src/debug_out.c`

На production-нагрузке CMD_RSP_TIMEOUT фиксируется нечасто, не эскалирует до recovery (ErrorCode сбрасывается перед следующей операцией). Документ сохранён как референс по природе ошибки; дальнейших действий не требует.
