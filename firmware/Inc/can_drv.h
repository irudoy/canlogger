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

#endif // CAN_DRV_H
