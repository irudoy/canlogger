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
  }
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
        // Mean-reverting random walk:
        // state = smoothing * state + (1-smoothing) * random_target
        // random_target drifts around, mean-reverts to midpoint
        float target = mid + (range / 2.0f) * rand_float(&s->seed);
        s->state = p->smoothing * s->state + (1.0f - p->smoothing) * target;
        // Clamp to range
        if (s->state < p->min_val) s->state = p->min_val;
        if (s->state > p->max_val) s->state = p->max_val;
        display_val = s->state;
        break;
      }

      case DEMO_CONST:
        display_val = p->min_val;
        break;

      default:
        break;
    }

    write_field_value(values, offset, field_types[i],
                      display_val, field_scales[i], field_offsets[i]);
    offset += fsize;
  }
}
