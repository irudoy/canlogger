#ifndef CAN_DRV_H
#define CAN_DRV_H

#include "ring_buf.h"

// Initialize CAN1 peripheral (PB8 RX, PB9 TX) at 500 kbit/s.
// Pass ring buffer for received frames.
int can_drv_init(ring_Buffer* rb);

// Start CAN reception (enables interrupt).
int can_drv_start(void);

// Stop CAN reception.
void can_drv_stop(void);

#endif // CAN_DRV_H
