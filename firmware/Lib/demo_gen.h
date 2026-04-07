#ifndef DEMO_GEN_H
#define DEMO_GEN_H

#include <stdint.h>
#include <stdlib.h>
#include "cfg_limits.h"

// Demo waveform types
typedef enum {
  DEMO_NONE = 0,   // not a demo field (real CAN)
  DEMO_SINE,       // sine wave
  DEMO_RAMP,       // sawtooth
  DEMO_SQUARE,     // square wave
  DEMO_NOISE,      // smooth random walk (mean-reverting)
  DEMO_CONST       // constant value
} demo_Func;

// Per-field demo parameters (parsed from config)
typedef struct {
  demo_Func func;
  float     min_val;
  float     max_val;
  uint32_t  period_ms;   // period for sine/ramp/square (default 5000)
  float     smoothing;   // noise smoothing 0.0-1.0 (default 0.95)
} demo_FieldParams;

// Per-field runtime state
typedef struct {
  float state;           // current value for noise generator
  uint32_t seed;         // per-field PRNG state
} demo_FieldState;

// Overall demo generator state
typedef struct {
  demo_FieldParams params[CFG_MAX_FIELDS];
  demo_FieldState  fstate[CFG_MAX_FIELDS];
  uint16_t         num_fields;
  uint8_t          enabled;    // 1 if demo mode active
} demo_Gen;

// Parse demo_func string to enum
demo_Func demo_parse_func(const char* s);

// Initialize generator state (call after config parsed)
void demo_init(demo_Gen* gen);

// Generate one tick of demo data, write into field_values buffer.
// tick_ms = current system time in ms.
// field_types = array of mlg_FieldType for each field.
// field_scales/field_offsets = scale/offset for MLG raw conversion.
void demo_generate(demo_Gen* gen, uint8_t* values, size_t record_length,
                   const uint8_t* field_types, const float* field_scales,
                   const float* field_offsets, uint16_t num_fields,
                   uint32_t tick_ms);

#endif // DEMO_GEN_H
