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

static uint8_t last_can640[8];
static uint8_t can640_valid = 0;

void debug_out_set_can640(const uint8_t* data, uint8_t dlc) {
  uint8_t len = dlc < 8 ? dlc : 8;
  for (uint8_t i = 0; i < len; i++) last_can640[i] = data[i];
  can640_valid = 1;
}

void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok) {
  uint32_t now = HAL_GetTick();
  if (now - last_tick >= 1000) {
    printf("[%lu] frames=%lu fields=%u init=%d\r\n",
           now / 1000, frames_processed, num_fields, init_ok);
    if (can640_valid) {
      uint16_t a1 = (last_can640[0] << 8) | last_can640[1]; // BE
      uint16_t a2 = (last_can640[2] << 8) | last_can640[3];
      printf("  0x640: %02X %02X %02X %02X %02X %02X %02X %02X  A1=%umV A2=%umV\r\n",
             last_can640[0], last_can640[1], last_can640[2], last_can640[3],
             last_can640[4], last_can640[5], last_can640[6], last_can640[7],
             a1, a2);
    }
    last_tick = now;
  }
}
