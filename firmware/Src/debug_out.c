#include "debug_out.h"
#include "usbd_cdc_if.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

#define CDC_TX_BUF_SIZE 640
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

void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok) {
  uint32_t now = HAL_GetTick();
  if (now - last_tick >= 1000) {
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
