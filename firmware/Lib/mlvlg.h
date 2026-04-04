#ifndef MLVLG_H
#define MLVLG_H

#include <stdint.h>
#include <stdlib.h>

#define MLG_FIELD_SIZE 89
#define MLG_FIELD_NAME_SIZE 34
#define MLG_FIELD_UNITS_SIZE 10
#define MLG_FIELD_CATEGORY_SIZE 34
#define MLG_HEADER_SIZE 24
#define MLG_MARKER_MESSAGE_SIZE 50

// Field types (scalar)
typedef enum {
  MLG_U08 = 0,
  MLG_S08 = 1,
  MLG_U16 = 2,
  MLG_S16 = 3,
  MLG_U32 = 4,
  MLG_S32 = 5,
  MLG_S64 = 6,
  MLG_F32 = 7
} mlg_FieldType;

// Display styles
typedef enum {
  MLG_FLOAT = 0,
  MLG_HEX = 1,
  MLG_BITS = 2,
  MLG_DATE = 3,
  MLG_ON_OFF = 4,
  MLG_YES_NO = 5,
  MLG_HIGH_LOW = 6,
  MLG_ACTIVE_INACTIVE = 7,
  MLG_TRUE_FALSE = 8
} mlg_DisplayStyle;

// Scalar logger field descriptor
typedef struct {
  uint8_t type;
  char name[MLG_FIELD_NAME_SIZE];
  char units[MLG_FIELD_UNITS_SIZE];
  uint8_t display_style;
  float scale;
  float transform;
  int8_t digits;
  char category[MLG_FIELD_CATEGORY_SIZE];
} mlg_Field;

// File header (24 bytes on wire)
typedef struct {
  uint8_t file_format[6];     // "MLVLG\0"
  uint16_t format_version;    // 0x0002
  uint32_t timestamp;         // unix timestamp
  uint32_t info_data_start;   // offset to info data (0 if none)
  uint32_t data_begin_index;  // offset to first data block
  uint16_t record_length;     // sum of field data sizes
  uint16_t num_fields;        // number of logger fields
} mlg_Header;

// Byte-swap copy (little-endian host → big-endian wire)
void mlg_swapend(void* dest, const void* src, size_t num);

// Returns the byte size of a field's data value
size_t mlg_field_data_size(mlg_FieldType type);

// Compute total record length (sum of all field data sizes)
size_t mlg_record_length(const mlg_Field* fields, size_t num_fields);

// Serialize a field descriptor (89 bytes) into buffer. Returns bytes written or -1 on error.
int mlg_write_field(uint8_t* buf, size_t buf_size, const mlg_Field* field);

// Serialize file header (24 bytes) into buffer. Returns bytes written or -1 on error.
int mlg_write_header(uint8_t* buf, size_t buf_size, const mlg_Header* header);

// Serialize a data block: type(1) + counter(1) + timestamp(2) + data(record_length) + crc(1).
// `data` must be pre-packed field values in big-endian. Returns bytes written or -1 on error.
int mlg_write_data_block(uint8_t* buf, size_t buf_size,
                         uint8_t counter, uint16_t timestamp_10us,
                         const uint8_t* data, size_t data_len);

// Serialize a marker block: type(1) + counter(1) + timestamp(2) + message(50).
// Returns bytes written (54) or -1 on error.
int mlg_write_marker(uint8_t* buf, size_t buf_size,
                     uint8_t counter, uint16_t timestamp,
                     const char* message);

#endif // MLVLG_H
