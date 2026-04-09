#include "demo_gen.h"
#include "mlvlg.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

demo_Func demo_parse_func(const char* s) {
  if (strcmp(s, "sine") == 0)   return DEMO_SINE;
  if (strcmp(s, "ramp") == 0)   return DEMO_RAMP;
  if (strcmp(s, "square") == 0) return DEMO_SQUARE;
  if (strcmp(s, "noise") == 0)  return DEMO_NOISE;
  if (strcmp(s, "const") == 0)  return DEMO_CONST;
  return DEMO_NONE;
}

// Simple xorshift32 PRNG — lightweight, good enough for demo data
static uint32_t xorshift32(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

// Returns float in [-1.0, 1.0]
static float rand_float(uint32_t* seed) {
  uint32_t r = xorshift32(seed);
  return (float)(int32_t)r / 2147483648.0f;
}

void demo_init(demo_Gen* gen) {
  for (int i = 0; i < gen->num_fields; i++) {
    demo_FieldParams* p = &gen->params[i];
    demo_FieldState* s = &gen->fstate[i];

    // Default period
    if (p->period_ms == 0) p->period_ms = 5000;

    // Default smoothing for noise
    if (p->func == DEMO_NOISE && p->smoothing == 0.0f)
      p->smoothing = 0.95f;

    // Init noise state to midpoint
    s->state = (p->min_val + p->max_val) / 2.0f;

    // Seed PRNG per field — different seeds for variety
    s->seed = 12345 + i * 7919;

    // Generate random phases for noise octaves
    for (int k = 0; k < 3; k++) {
      s->phase[k] = (rand_float(&s->seed) + 1.0f) * M_PI; // [0, 2*PI]
    }
  }

  // Ensure first call to demo_pack_can_frames always generates
  gen->last_pack_tick = UINT32_MAX;
}

// Convert display value to raw MLG value and write big-endian into buffer
static void write_field_value(uint8_t* buf, size_t offset, uint8_t type,
                              float display_val, float scale, float field_offset) {
  // MLG: DisplayValue = (rawValue + transform) * scale
  // where transform = offset (in our config terms)
  // So: rawValue = DisplayValue / scale - transform
  float raw_f = (scale != 0.0f) ? (display_val / scale - field_offset) : 0.0f;

  uint8_t* p = buf + offset;
  switch (type) {
    case MLG_U08: {
      int32_t v = (int32_t)raw_f;
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      p[0] = (uint8_t)v;
      break;
    }
    case MLG_S08: {
      int32_t v = (int32_t)raw_f;
      if (v < -128) v = -128;
      if (v > 127) v = 127;
      p[0] = (uint8_t)(int8_t)v;
      break;
    }
    case MLG_U16: {
      int32_t v = (int32_t)raw_f;
      if (v < 0) v = 0;
      if (v > 65535) v = 65535;
      p[0] = (uint8_t)(v >> 8);
      p[1] = (uint8_t)(v & 0xFF);
      break;
    }
    case MLG_S16: {
      int32_t v = (int32_t)raw_f;
      if (v < -32768) v = -32768;
      if (v > 32767) v = 32767;
      p[0] = (uint8_t)(v >> 8);
      p[1] = (uint8_t)(v & 0xFF);
      break;
    }
    case MLG_U32: {
      uint32_t v = (raw_f < 0) ? 0 : (uint32_t)raw_f;
      p[0] = (uint8_t)(v >> 24);
      p[1] = (uint8_t)(v >> 16);
      p[2] = (uint8_t)(v >> 8);
      p[3] = (uint8_t)(v);
      break;
    }
    case MLG_S32: {
      int32_t v = (int32_t)raw_f;
      p[0] = (uint8_t)(v >> 24);
      p[1] = (uint8_t)(v >> 16);
      p[2] = (uint8_t)(v >> 8);
      p[3] = (uint8_t)(v);
      break;
    }
    case MLG_F32: {
      float v = raw_f;
      uint32_t bits;
      memcpy(&bits, &v, 4);
      p[0] = (uint8_t)(bits >> 24);
      p[1] = (uint8_t)(bits >> 16);
      p[2] = (uint8_t)(bits >> 8);
      p[3] = (uint8_t)(bits);
      break;
    }
    default:
      break;
  }
}

float demo_compute_waveform(demo_FieldParams* p, demo_FieldState* s, uint32_t tick_ms) {
  if (p->func == DEMO_NONE) return 0.0f;

  float range = p->max_val - p->min_val;
  float mid = (p->min_val + p->max_val) / 2.0f;
  float phase = (float)(tick_ms % p->period_ms) / (float)p->period_ms;
  float display_val = mid;

  switch (p->func) {
    case DEMO_SINE:
      display_val = mid + (range / 2.0f) * sinf(2.0f * M_PI * phase);
      break;

    case DEMO_RAMP:
      display_val = p->min_val + range * phase;
      break;

    case DEMO_SQUARE:
      display_val = (phase < 0.5f) ? p->max_val : p->min_val;
      break;

    case DEMO_NOISE: {
      // Sum of 3 sine octaves with random phases — smooth, realistic signal.
      // Frequencies: base, 2.71x, 7.3x (irrational ratios avoid repetition)
      float t = 2.0f * M_PI * phase;
      float wave = 0.6f * sinf(t + s->phase[0])
                 + 0.3f * sinf(2.71f * t + s->phase[1])
                 + 0.1f * sinf(7.3f * t + s->phase[2]);
      // Add small jitter (1% of range)
      float jitter = 0.01f * range * rand_float(&s->seed);
      display_val = mid + (range / 2.0f) * wave + jitter;
      // Clamp to range
      if (display_val < p->min_val) display_val = p->min_val;
      if (display_val > p->max_val) display_val = p->max_val;
      break;
    }

    case DEMO_CONST:
      display_val = p->min_val;
      break;

    default:
      break;
  }

  return display_val;
}

void demo_generate(demo_Gen* gen, uint8_t* values, size_t record_length,
                   const uint8_t* field_types, const float* field_scales,
                   const float* field_offsets, uint16_t num_fields,
                   uint32_t tick_ms) {
  (void)record_length;

  size_t offset = 0;
  for (int i = 0; i < num_fields; i++) {
    size_t fsize = mlg_field_data_size((mlg_FieldType)field_types[i]);
    demo_FieldParams* p = &gen->params[i];
    demo_FieldState* s = &gen->fstate[i];

    if (p->func == DEMO_NONE) {
      offset += fsize;
      continue;
    }

    float display_val = demo_compute_waveform(p, s, tick_ms);

    write_field_value(values, offset, field_types[i],
                      display_val, field_scales[i], field_offsets[i]);
    offset += fsize;
  }
}
