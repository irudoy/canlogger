#ifndef DEBUG_OUT_H
#define DEBUG_OUT_H

#include <stdint.h>
#include "config.h"
#include "log_writer.h"
#include "ring_buf.h"

// Call from main loop. Prints periodic status over USB CDC.
void debug_out_tick(uint32_t frames_processed, uint16_t num_fields, int init_ok);

// Capture any CAN frame for debug display.
void debug_out_set_can(uint32_t id, const uint8_t* data, uint8_t dlc);

// Called from CDC_Receive_FS when data arrives over USB.
void debug_cmd_receive(const uint8_t* buf, uint32_t len);

// Poll for pending commands. Call from main loop.
void debug_cmd_poll(const cfg_Config* cfg, int init_ok, const ring_Buffer* rb);

#endif
