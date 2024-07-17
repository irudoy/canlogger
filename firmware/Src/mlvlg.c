#include "mlvlg.h"
#include <string.h>
#include <stdlib.h>

extern RTC_HandleTypeDef hrtc;

uint32_t mlvlg_calculate_crc32(const uint8_t* data, uint32_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

void mlvlg_init_header(mlvlg_header* header, uint32_t num_fields) {
  memset(header, 0, sizeof(mlvlg_header));
  memcpy(header->magic_number, MLVLG_MAGIC_NUMBER, sizeof(header->magic_number));
  header->version = MLVLG_VERSION;
  mlvlg_datetime datetime;
  mlvlg_get_current_datetime(&datetime);
  header->timestamp = mlvlg_generate_timestamp(&datetime);
  header->info_data_start = 0;
  header->data_begin_idx = sizeof(mlvlg_header) + num_fields * 55;
  header->record_length = sizeof(mlvlg_record);
  header->num_logger_fields = num_fields;
  header->crc32 = 0;
}

void mlvlg_generate_header_bytes(const mlvlg_header* header, const void* fields, uint32_t num_fields, uint8_t* buffer) {
  memcpy(buffer, header->magic_number, sizeof(header->magic_number));
  memcpy(buffer + 6, &header->version, sizeof(header->version));
  memcpy(buffer + 8, &header->timestamp, sizeof(header->timestamp));
  memcpy(buffer + 12, &header->info_data_start, sizeof(header->info_data_start));
  memcpy(buffer + 14, &header->data_begin_idx, sizeof(header->data_begin_idx));
  memcpy(buffer + 18, &header->record_length, sizeof(header->record_length));
  memcpy(buffer + 20, &header->num_logger_fields, sizeof(header->num_logger_fields));

  const uint8_t* fields_ptr = (const uint8_t*)fields;
  for (uint32_t i = 0; i < num_fields; ++i) {
    memcpy(buffer + 22 + i * 55, fields_ptr + i * 55, 55);
  }

  uint32_t crc = mlvlg_calculate_crc32(buffer, 22 + num_fields * 55);
  memcpy(buffer + 22 + num_fields * 55, &crc, sizeof(crc));
}

void mlvlg_generate_record_bytes(const mlvlg_record* record, uint8_t* buffer) {
  memcpy(buffer, &record->timestamp, sizeof(record->timestamp));
  buffer[4] = record->id;
  memcpy(buffer + 5, &record->value, sizeof(record->value));
}

mlvlg_field_type mlvlg_resolve_type(const char* type_str) {
  if (strcmp(type_str, "U08") == 0) return mlvlg_field_u08;
  if (strcmp(type_str, "S08") == 0) return mlvlg_field_s08;
  if (strcmp(type_str, "U16") == 0) return mlvlg_field_u16;
  if (strcmp(type_str, "S16") == 0) return mlvlg_field_s16;
  if (strcmp(type_str, "U32") == 0) return mlvlg_field_u32;
  if (strcmp(type_str, "S32") == 0) return mlvlg_field_s32;
  if (strcmp(type_str, "S64") == 0) return mlvlg_field_s64;
  if (strcmp(type_str, "F32") == 0) return mlvlg_field_f32;
  if (strcmp(type_str, "U08_BITFIELD") == 0) return mlvlg_field_u08_bitfield;
  if (strcmp(type_str, "U16_BITFIELD") == 0) return mlvlg_field_u16_bitfield;
  if (strcmp(type_str, "U32_BITFIELD") == 0) return mlvlg_field_u32_bitfield;
  return mlvlg_field_f32;
}

void mlvlg_get_current_datetime(mlvlg_datetime* datetime) {
  datetime->year = 2024;
  datetime->month = 7;
  datetime->day = 17;
  datetime->hour = 12;
  datetime->minute = 0;
  datetime->second = 0;
}

uint32_t mlvlg_generate_timestamp(const mlvlg_datetime* datetime) {
  uint32_t days = datetime->day - 1;
  uint32_t months = datetime->month - 1;
  uint32_t years = datetime->year - 1970;

  uint32_t timestamp = years * 31536000 + months * 2592000 + days * 86400;
  timestamp += datetime->hour * 3600 + datetime->minute * 60 + datetime->second;

  return timestamp;
}
