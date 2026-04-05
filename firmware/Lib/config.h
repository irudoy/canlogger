#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdlib.h>

#define CFG_MAX_FIELDS 32
#define CFG_NAME_SIZE  34
#define CFG_UNITS_SIZE 10
#define CFG_CAT_SIZE   34
#define CFG_LUT_MAX    16

typedef struct {
  uint16_t input;
  int16_t  output;
} cfg_LutPoint;

typedef struct {
  uint32_t can_id;
  uint8_t  start_byte;    // 0-7
  uint8_t  bit_length;    // 8, 16, 32, 64
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
} cfg_Field;

#define CFG_MAX_CAN_IDS 32

typedef struct {
  uint32_t  log_interval_ms;
  uint32_t  can_bitrate;             // 125000, 250000, 500000, 1000000 (default 500000)
  cfg_Field fields[CFG_MAX_FIELDS];
  uint16_t  num_fields;
  // Derived: unique CAN IDs from fields (for hardware filter setup)
  uint32_t  can_ids[CFG_MAX_CAN_IDS];
  uint16_t  num_can_ids;
} cfg_Config;

// Error codes
#define CFG_OK             0
#define CFG_ERR_SYNTAX    -1
#define CFG_ERR_OVERFLOW  -2  // too many fields
#define CFG_ERR_MISSING   -3  // required field missing
#define CFG_ERR_VALUE     -4  // invalid value

// Parse INI text from buffer. Returns CFG_OK or error code.
int cfg_parse(const char* text, size_t len, cfg_Config* out);

#endif // CONFIG_H
