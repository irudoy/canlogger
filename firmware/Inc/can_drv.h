#ifndef CAN_DRV_H
#define CAN_DRV_H

#include "ring_buf.h"
#include "config.h"

// Initialize CAN1 with bitrate and filters from config.
// Re-initializes CAN peripheral with correct timing.
int can_drv_init(ring_Buffer* rb, const cfg_Config* cfg);

// Start CAN reception (enables interrupt).
int can_drv_start(void);

// Stop CAN reception.
void can_drv_stop(void);

// CAN bus diagnostics (from ESR register)
typedef struct {
  uint8_t  tec;        // Transmit Error Counter
  uint8_t  rec;        // Receive Error Counter
  uint8_t  bus_state;  // 0=Active, 1=Passive, 2=Bus-Off
  uint8_t  lec;        // Last Error Code (0=none,1=stuff,2=form,3=ack,4=bit_rec,5=bit_dom,6=crc)
  uint32_t rx_overrun; // FIFO0 overrun count
} can_Diag;

void can_drv_get_diag(can_Diag* out);

#endif // CAN_DRV_H
