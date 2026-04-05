#ifndef DEBUG_OUT_H
#define DEBUG_OUT_H

#include <stdint.h>

// Call from main loop. Prints periodic status over USB CDC.
void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok);

// Capture any CAN frame for debug display.
void debug_out_set_can(uint32_t id, const uint8_t* data, uint8_t dlc);

#endif
