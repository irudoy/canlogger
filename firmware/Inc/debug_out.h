#ifndef DEBUG_OUT_H
#define DEBUG_OUT_H

#include <stdint.h>

// Call from main loop. Prints periodic status over USB CDC.
void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok);

#endif
