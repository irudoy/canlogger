#include "gps_drv.h"

#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"

// From main.c — USART3 HAL handle and its RX DMA stream handle.
extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

// Circular DMA target — must live in main SRAM (DMA1 has no CCM access).
static uint8_t  rx_dma_buf[GPS_RX_DMA_BUF_SIZE];
static uint16_t rx_read_pos;

static gps_LineBuffer lb;
static gps_State      state;

static uint32_t cnt_ok, cnt_bad, cnt_ignored;
static uint8_t  raw_enabled;

void gps_drv_init(void) {
  rx_read_pos = 0;
  memset(rx_dma_buf, 0, sizeof(rx_dma_buf));
  gps_lb_init(&lb);
  gps_state_init(&state);
  cnt_ok = cnt_bad = cnt_ignored = 0;
  // Circular mode was set in CubeMX (.ioc): DMA wraps automatically,
  // HAL_UART_Receive_DMA never "finishes", callbacks fire periodically
  // and are ignored — we poll NDTR in gps_drv_poll() instead.
  HAL_UART_Receive_DMA(&huart3, rx_dma_buf, GPS_RX_DMA_BUF_SIZE);
}

int gps_drv_poll(void) {
  const uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
  const uint16_t write_pos = (uint16_t)(GPS_RX_DMA_BUF_SIZE - ndtr);
  int parsed = 0;
  while (rx_read_pos != write_pos) {
    uint8_t byte = rx_dma_buf[rx_read_pos];
    rx_read_pos = (uint16_t)((rx_read_pos + 1) % GPS_RX_DMA_BUF_SIZE);
    if (raw_enabled) {
      // Strip CR so host terminals don't double-newline.
      if (byte != '\r') putchar((int)byte);
    }
    const char* emitted;
    if (gps_lb_feed_byte(&lb, byte, &emitted)) {
      switch (gps_parse_sentence(&state, emitted)) {
        case GPS_PARSE_OK:       ++cnt_ok; ++parsed; break;
        case GPS_PARSE_IGNORED:  ++cnt_ignored; break;
        default:                 ++cnt_bad; break;
      }
    }
  }
  return parsed;
}

void gps_drv_set_raw(uint8_t enabled) { raw_enabled = enabled ? 1 : 0; }
uint8_t gps_drv_get_raw(void) { return raw_enabled; }

const gps_State* gps_drv_state(void)    { return &state; }
uint32_t gps_drv_count_ok(void)         { return cnt_ok; }
uint32_t gps_drv_count_bad(void)        { return cnt_bad; }
uint32_t gps_drv_count_ignored(void)    { return cnt_ignored; }
uint8_t  gps_drv_lb_overflow(void)      { return lb.overflow; }
