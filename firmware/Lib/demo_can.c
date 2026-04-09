#include "demo_can.h"
#include "demo_gen.h"
#include "mlvlg.h"
#include <string.h>

// Inverse LUT: given a display-unit value, find the raw uint16 input.
// Uses linear interpolation between LUT points (reverse direction).
static uint16_t lut_inverse(const cfg_LutPoint* lut, uint8_t count, float display_val) {
  // Determine if LUT is ascending or descending in output
  int ascending = (lut[count - 1].output >= lut[0].output);

  // Clamp to LUT output range
  if (ascending) {
    if (display_val <= (float)lut[0].output) return lut[0].input;
    if (display_val >= (float)lut[count - 1].output) return lut[count - 1].input;
  } else {
    if (display_val >= (float)lut[0].output) return lut[0].input;
    if (display_val <= (float)lut[count - 1].output) return lut[count - 1].input;
  }

  // Linear search + interpolation
  for (int i = 0; i < count - 1; i++) {
    float out_lo = (float)lut[i].output;
    float out_hi = (float)lut[i + 1].output;

    // Check if display_val falls between these two points
    int in_range = ascending
      ? (display_val >= out_lo && display_val <= out_hi)
      : (display_val <= out_lo && display_val >= out_hi);

    if (in_range) {
      float denom = out_hi - out_lo;
      if (denom == 0.0f) return lut[i].input;
      float frac = (display_val - out_lo) / denom;
      float input_f = (float)lut[i].input + frac * (float)(lut[i + 1].input - lut[i].input);
      return (uint16_t)(input_f + 0.5f);
    }
  }

  return lut[count - 1].input;
}

// Pack a raw integer value into CAN frame data at start_byte, respecting endianness.
// byte_count = number of bytes (1, 2, or 4).
static void pack_can_bytes(uint8_t* data, uint8_t start_byte, uint8_t byte_count,
                           uint8_t is_big_endian, uint32_t raw) {
  if (is_big_endian) {
    for (int i = byte_count - 1; i >= 0; i--) {
      data[start_byte + i] = (uint8_t)(raw & 0xFF);
      raw >>= 8;
    }
  } else {
    for (int i = 0; i < byte_count; i++) {
      data[start_byte + i] = (uint8_t)(raw & 0xFF);
      raw >>= 8;
    }
  }
}

int demo_pack_can_frames(cfg_Config* cfg, ring_Buffer* rb, uint32_t tick_ms) {
  demo_Gen* gen = &cfg->demo_gen;

  // Rate limiting: only generate once per log_interval_ms
  // UINT32_MAX = sentinel from demo_init → always allow first call
  if (gen->last_pack_tick != UINT32_MAX &&
      tick_ms - gen->last_pack_tick < cfg->log_interval_ms) return 0;
  gen->last_pack_tick = tick_ms;

  int frames_pushed = 0;

  // For each unique CAN ID, build a frame
  for (int c = 0; c < cfg->num_can_ids; c++) {
    uint32_t can_id = cfg->can_ids[c];
    can_Frame frame;
    frame.id = can_id;
    frame.dlc = 8;
    memset(frame.data, 0, 8);

    int has_demo_field = 0;

    // Fill in fields that map to this CAN ID
    for (int i = 0; i < cfg->num_fields; i++) {
      if (cfg->fields[i].can_id != can_id) continue;
      if (gen->params[i].func == DEMO_NONE) continue;

      has_demo_field = 1;

      float display_val = demo_compute_waveform(&gen->params[i], &gen->fstate[i], tick_ms);

      // Convert display value to raw CAN value
      uint32_t raw;
      cfg_Field* f = &cfg->fields[i];
      uint8_t byte_count = f->bit_length / 8;
      if (byte_count == 0) byte_count = 1;

      if (f->lut_count >= 2) {
        // Inverse LUT: display → raw input
        raw = lut_inverse(f->lut, f->lut_count, display_val);
      } else {
        // Inverse scale/offset: raw = display / scale - offset
        float raw_f = (f->scale != 0.0f) ? (display_val / f->scale - f->offset) : 0.0f;
        // Round and clamp based on type
        int32_t ival = (int32_t)(raw_f >= 0 ? raw_f + 0.5f : raw_f - 0.5f);
        switch (f->type) {
          case MLG_U08:
            if (ival < 0) ival = 0;
            if (ival > 255) ival = 255;
            break;
          case MLG_S08:
            if (ival < -128) ival = -128;
            if (ival > 127) ival = 127;
            break;
          case MLG_U16:
            if (ival < 0) ival = 0;
            if (ival > 65535) ival = 65535;
            break;
          case MLG_S16:
            if (ival < -32768) ival = -32768;
            if (ival > 32767) ival = 32767;
            break;
          default:
            break;
        }
        raw = (uint32_t)ival;
      }

      pack_can_bytes(frame.data, f->start_byte, byte_count, f->is_big_endian, raw);
    }

    if (!has_demo_field) continue;

    if (ring_buf_push(rb, &frame) != 0) return -1;
    frames_pushed++;
  }

  return frames_pushed;
}
