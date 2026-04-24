#ifndef GPS_DRV_H
#define GPS_DRV_H

#include <stdint.h>
#include "gps_nmea.h"

#define GPS_RX_DMA_BUF_SIZE 128

// Start DMA RX on USART3 into the circular capture buffer.
// Must be called after MX_USART3_UART_Init() and MX_DMA_Init(). Idempotent.
void gps_drv_init(void);

// Drain bytes arrived since last poll, assemble sentences, parse them
// into the internal gps_State. Thread-context: task_producer.
// Returns the number of complete NMEA sentences decoded in this call
// that the parser recognized (GPS_PARSE_OK).
int gps_drv_poll(void);

// Snapshot of the latest parsed GPS state. Caller is responsible for
// synchronization — in canlogger we read/write under shadow_mutex.
const gps_State* gps_drv_state(void);

// Counters for CDC status / debug.
uint32_t gps_drv_count_ok(void);
uint32_t gps_drv_count_bad(void);
uint32_t gps_drv_count_ignored(void);
uint8_t  gps_drv_lb_overflow(void);

// Raw NMEA byte tap: when enabled, gps_drv_poll() prints each received
// byte to the CDC stdout (via printf/__io_putchar) as it drains the DMA
// buffer. Toggle via CDC `gps_raw` command. No effect on parsing.
void gps_drv_set_raw(uint8_t enabled);
uint8_t gps_drv_get_raw(void);

#endif  // GPS_DRV_H
