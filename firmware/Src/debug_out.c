#include "debug_out.h"
#include "usbd_cdc_if.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

#define CDC_TX_BUF_SIZE 256
static uint8_t cdc_tx_buf[CDC_TX_BUF_SIZE];
static uint16_t cdc_tx_pos = 0;
static uint32_t last_tick = 0;

// Flush buffer over USB CDC. Silently drops data if USB is busy
// (host not connected or previous transfer in progress) — acceptable
// for debug output, avoids blocking the main loop.
static void cdc_flush(void) {
  if (cdc_tx_pos > 0) {
    CDC_Transmit_FS(cdc_tx_buf, cdc_tx_pos);
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

void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok) {
  uint32_t now = HAL_GetTick();
  if (now - last_tick >= 1000) {
    printf("[%lu] frames=%lu fields=%u init=%d\r\n",
           now / 1000, frames_processed, num_fields, init_ok);
    last_tick = now;
  }
}
