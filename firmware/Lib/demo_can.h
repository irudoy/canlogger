#ifndef DEMO_CAN_H
#define DEMO_CAN_H

#include "config.h"
#include "ring_buf.h"

// Pack demo-generated data into CAN frames and push to ring buffer.
// Rate-limited internally by log_interval_ms via demo_gen.last_pack_tick.
// Returns number of frames pushed, or -1 if ring buffer full.
int demo_pack_can_frames(cfg_Config* cfg, ring_Buffer* rb, uint32_t tick_ms);

#endif // DEMO_CAN_H
