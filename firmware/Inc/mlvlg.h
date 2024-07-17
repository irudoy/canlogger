#ifndef MLVLG_H
#define MLVLG_H

#include <stdint.h>
#include "main.h"

#define MLVLG_MAGIC_NUMBER "MLVLG\x00"
#define MLVLG_VERSION 0x0100

typedef enum {
  mlvlg_field_u08 = 0,
  mlvlg_field_s08 = 1,
  mlvlg_field_u16 = 2,
  mlvlg_field_s16 = 3,
  mlvlg_field_u32 = 4,
  mlvlg_field_s32 = 5,
  mlvlg_field_s64 = 6,
  mlvlg_field_f32 = 7,
  mlvlg_field_u08_bitfield = 10,
  mlvlg_field_u16_bitfield = 11,
  mlvlg_field_u32_bitfield = 12
} mlvlg_field_type;

typedef enum {
  mlvlg_display_float = 0,
  mlvlg_display_hex = 1,
  mlvlg_display_bits = 2,
  mlvlg_display_date = 3,
  mlvlg_display_on_off = 4,
  mlvlg_display_yes_no = 5,
  mlvlg_display_high_low = 6,
  mlvlg_display_active_inactive = 7
} mlvlg_display_style;

typedef struct {
  char magic_number[6];
  uint16_t version;
  uint32_t timestamp;
  uint16_t info_data_start;
  uint32_t data_begin_idx;
  uint16_t record_length;
  uint16_t num_logger_fields;
  uint32_t crc32;
} mlvlg_header;

typedef struct {
  uint32_t timestamp;
  uint8_t id;
  float value;
} mlvlg_record;

typedef struct {
  mlvlg_field_type type;
  char name[34];
  char units[10];
  mlvlg_display_style display_style;
  float scale;
  float transform;
  int8_t digits;
} mlvlg_scalar_field;

typedef struct {
  mlvlg_field_type type;
  char name[34];
  char units[10];
  mlvlg_display_style display_style;
  uint8_t bit_field_style;
  uint32_t bit_field_names_index;
  uint8_t bits;
  uint8_t unused[3];
} mlvlg_bit_field;

typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} mlvlg_datetime;

void mlvlg_init_header(mlvlg_header* header, uint32_t num_fields);
void mlvlg_generate_header_bytes(const mlvlg_header* header, const void* fields, uint32_t num_fields, uint8_t* buffer);
void mlvlg_generate_record_bytes(const mlvlg_record* record, uint8_t* buffer);
mlvlg_field_type mlvlg_resolve_type(const char* type_str);
void mlvlg_get_current_datetime(mlvlg_datetime* datetime);
uint32_t mlvlg_generate_timestamp(const mlvlg_datetime* datetime);

#endif // MLVLG_H
