#include "mlvlg.h"
#include <string.h>

// Assumes little-endian host (x86, ARM Cortex-M). Converts to big-endian wire format.
void mlg_swapend(void* dest, const void* src, size_t num) {
  const uint8_t* s = (const uint8_t*)src;
  uint8_t* d = (uint8_t*)dest;
  for (size_t i = 0; i < num; i++) {
    d[i] = s[num - 1 - i];
  }
}

size_t mlg_field_data_size(mlg_FieldType type) {
  switch (type) {
    case MLG_U08: case MLG_S08: return 1;
    case MLG_U16: case MLG_S16: return 2;
    case MLG_U32: case MLG_S32: case MLG_F32: return 4;
    case MLG_S64: return 8;
    default: return 0;
  }
}

size_t mlg_record_length(const mlg_Field* fields, size_t num_fields) {
  size_t total = 0;
  for (size_t i = 0; i < num_fields; i++) {
    total += mlg_field_data_size((mlg_FieldType)fields[i].type);
  }
  return total;
}

int mlg_write_field(uint8_t* buf, size_t buf_size, const mlg_Field* field) {
  if (buf_size < MLG_FIELD_SIZE) return -1;
  memset(buf, 0, MLG_FIELD_SIZE);

  size_t off = 0;

  // Type (1 byte)
  buf[off++] = field->type;

  // Name (34 bytes)
  memcpy(buf + off, field->name, MLG_FIELD_NAME_SIZE);
  off += MLG_FIELD_NAME_SIZE;

  // Units (10 bytes)
  memcpy(buf + off, field->units, MLG_FIELD_UNITS_SIZE);
  off += MLG_FIELD_UNITS_SIZE;

  // Display style (1 byte)
  buf[off++] = field->display_style;

  // Scale (4 bytes, big-endian float)
  mlg_swapend(buf + off, &field->scale, sizeof(float));
  off += sizeof(float);

  // Transform (4 bytes, big-endian float)
  mlg_swapend(buf + off, &field->transform, sizeof(float));
  off += sizeof(float);

  // Digits (1 byte, signed per spec)
  buf[off++] = field->digits;

  // Category (34 bytes)
  memcpy(buf + off, field->category, MLG_FIELD_CATEGORY_SIZE);

  return MLG_FIELD_SIZE;
}

int mlg_write_header(uint8_t* buf, size_t buf_size, const mlg_Header* header) {
  if (buf_size < MLG_HEADER_SIZE) return -1;
  memset(buf, 0, MLG_HEADER_SIZE);

  // File format (6 bytes)
  memcpy(buf, header->file_format, 6);

  // Format version (2 bytes BE)
  buf[6] = (header->format_version >> 8) & 0xFF;
  buf[7] = header->format_version & 0xFF;

  // Timestamp (4 bytes BE)
  buf[8]  = (header->timestamp >> 24) & 0xFF;
  buf[9]  = (header->timestamp >> 16) & 0xFF;
  buf[10] = (header->timestamp >> 8) & 0xFF;
  buf[11] = header->timestamp & 0xFF;

  // Info data start (4 bytes BE)
  buf[12] = (header->info_data_start >> 24) & 0xFF;
  buf[13] = (header->info_data_start >> 16) & 0xFF;
  buf[14] = (header->info_data_start >> 8) & 0xFF;
  buf[15] = header->info_data_start & 0xFF;

  // Data begin index (4 bytes BE)
  buf[16] = (header->data_begin_index >> 24) & 0xFF;
  buf[17] = (header->data_begin_index >> 16) & 0xFF;
  buf[18] = (header->data_begin_index >> 8) & 0xFF;
  buf[19] = header->data_begin_index & 0xFF;

  // Record length (2 bytes BE)
  buf[20] = (header->record_length >> 8) & 0xFF;
  buf[21] = header->record_length & 0xFF;

  // Num logger fields (2 bytes BE)
  buf[22] = (header->num_fields >> 8) & 0xFF;
  buf[23] = header->num_fields & 0xFF;

  return MLG_HEADER_SIZE;
}

int mlg_write_data_block(uint8_t* buf, size_t buf_size,
                         uint8_t counter, uint16_t timestamp_10us,
                         const uint8_t* data, size_t data_len) {
  size_t total = 1 + 1 + 2 + data_len + 1; // type + counter + ts + data + crc
  if (buf_size < total) return -1;

  size_t off = 0;

  // Block type = 0 (field data)
  buf[off++] = 0x00;

  // Counter
  buf[off++] = counter;

  // Timestamp (2 bytes BE, 10us resolution)
  buf[off++] = (timestamp_10us >> 8) & 0xFF;
  buf[off++] = timestamp_10us & 0xFF;

  // Field data
  memcpy(buf + off, data, data_len);

  // CRC: sum of data bytes only
  uint8_t crc = 0;
  for (size_t i = 0; i < data_len; i++) {
    crc += data[i];
  }
  buf[off + data_len] = crc;

  return (int)total;
}

int mlg_write_marker(uint8_t* buf, size_t buf_size,
                     uint8_t counter, uint16_t timestamp,
                     const char* message) {
  size_t total = 1 + 1 + 2 + MLG_MARKER_MESSAGE_SIZE; // 54
  if (buf_size < total) return -1;
  memset(buf, 0, total);

  buf[0] = 0x01; // marker type
  buf[1] = counter;
  buf[2] = (timestamp >> 8) & 0xFF;
  buf[3] = timestamp & 0xFF;

  if (message) {
    strncpy((char*)(buf + 4), message, MLG_MARKER_MESSAGE_SIZE);
  }

  return (int)total;
}
