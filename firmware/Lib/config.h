#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdlib.h>
#include "cfg_limits.h"
#include "demo_gen.h"
#define CFG_NAME_SIZE  34
#define CFG_UNITS_SIZE 10
#define CFG_CAT_SIZE   34
#define CFG_LUT_MAX    16

// invalid_strategy: what to do when extracted value fails valid_min/max check.
// Check is performed on the display-unit value after scale+offset (and LUT).
// Default = LAST_GOOD: repeats the previous known-good value (or 0 on first frame).
#define CFG_INVALID_LAST_GOOD 0
#define CFG_INVALID_CLAMP     1  // saturate to valid_min or valid_max
#define CFG_INVALID_SKIP      2  // don't update shadow buffer at all

// preset: per-field plug-in fault detector with custom logic the INI cannot
// express (e.g. AEM UEGO: value==65535 means sensor fault/warmup).
#define CFG_PRESET_NONE       0
#define CFG_PRESET_AEM_UEGO   1  // reject raw 0xFFFF on 16-bit Lambda/AFR

typedef struct {
  uint16_t input;
  int16_t  output;
} cfg_LutPoint;

typedef struct {
  uint32_t can_id;
  uint8_t  is_extended;   // 1 = 29-bit extended ID, 0 = 11-bit standard
  uint8_t  start_byte;    // 0-7
  uint8_t  start_bit;     // 0-7, for sub-byte fields (bit_length < 8)
  uint8_t  bit_length;    // 1-7 (sub-byte) or 8, 16, 32, 64
  uint8_t  is_big_endian; // byte order in CAN frame
  float    scale;
  float    offset;
  uint8_t  type;          // mlg_FieldType
  uint8_t  display_style;
  int8_t   digits;
  char     name[CFG_NAME_SIZE];
  char     units[CFG_UNITS_SIZE];
  char     category[CFG_CAT_SIZE];
  cfg_LutPoint lut[CFG_LUT_MAX];
  uint8_t  lut_count;
  // Plausibility / sanity filtering — applied after scale+offset (and LUT).
  // valid_min/valid_max are in display units. "has_" flags let us distinguish
  // "no limit set" from "limit set to 0.0".
  float    valid_min;
  float    valid_max;
  uint8_t  has_valid_min;
  uint8_t  has_valid_max;
  uint8_t  invalid_strategy; // CFG_INVALID_*
  uint8_t  preset;           // CFG_PRESET_*
} cfg_Field;

typedef struct {
  uint32_t  log_interval_ms;
  uint32_t  can_bitrate;             // 125000, 250000, 500000, 1000000 (default 500000)
  cfg_Field fields[CFG_MAX_FIELDS];
  uint16_t  num_fields;
  // Derived: unique CAN IDs from fields (for hardware filter setup)
  uint32_t  can_ids[CFG_MAX_CAN_IDS];
  uint8_t   can_ids_extended[CFG_MAX_CAN_IDS];  // parallel: 1 = 29-bit ext, 0 = std
  uint16_t  num_can_ids;
  // Demo mode (auto-detected: set if any field has demo_func)
  uint8_t   demo;
  demo_Gen  demo_gen;
} cfg_Config;

// Error codes
#define CFG_OK             0
#define CFG_ERR_SYNTAX    -1
#define CFG_ERR_OVERFLOW  -2  // too many fields
#define CFG_ERR_MISSING   -3  // required field missing
#define CFG_ERR_VALUE     -4  // invalid value

// Parse INI text from buffer. Returns CFG_OK or error code.
int cfg_parse(const char* text, size_t len, cfg_Config* out);

// Line reader callback: read one line into buf (max max_len), return length or -1 on EOF.
typedef int (*cfg_readline_fn)(char* buf, int max_len, void* ctx);

// Parse INI from a stream (line-by-line callback). No full-file buffer needed.
int cfg_parse_stream(cfg_readline_fn readline, void* ctx, cfg_Config* out);

#endif // CONFIG_H
