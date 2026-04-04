#include "can_map.h"
#include <string.h>

int can_map_init(can_FieldValues* fv, const cfg_Config* cfg) {
  memset(fv, 0, sizeof(can_FieldValues));
  fv->num_fields = cfg->num_fields;
  fv->record_length = 0;

  for (int i = 0; i < cfg->num_fields; i++) {
    fv->record_length += mlg_field_data_size((mlg_FieldType)cfg->fields[i].type);
  }

  if (fv->record_length > CAN_MAP_MAX_RECORD_SIZE) return -1;
  return 0;
}

// Extract a value from CAN data at given byte offset and bit_length.
// Writes result as big-endian into dest.
static void extract_value(const uint8_t* can_data, uint8_t start_byte, uint8_t bit_length,
                           uint8_t is_big_endian, uint8_t* dest) {
  size_t byte_count = bit_length / 8;
  if (byte_count == 0) byte_count = 1;

  if (is_big_endian) {
    // CAN data already big-endian, copy directly
    memcpy(dest, can_data + start_byte, byte_count);
  } else {
    // CAN data little-endian, swap to big-endian
    for (size_t i = 0; i < byte_count; i++) {
      dest[i] = can_data[start_byte + byte_count - 1 - i];
    }
  }
}

int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame) {
  int updated = 0;
  size_t offset = 0;

  for (int i = 0; i < cfg->num_fields; i++) {
    size_t field_size = mlg_field_data_size((mlg_FieldType)cfg->fields[i].type);

    if (cfg->fields[i].can_id == frame->id) {
      extract_value(frame->data, cfg->fields[i].start_byte,
                    cfg->fields[i].bit_length, cfg->fields[i].is_big_endian,
                    fv->values + offset);
      updated++;
    }

    offset += field_size;
  }

  if (updated > 0) fv->updated = 1;
  return updated;
}

void can_map_build_mlg_fields(const cfg_Config* cfg, mlg_Field* out, size_t max_fields) {
  size_t count = cfg->num_fields;
  if (count > max_fields) count = max_fields;

  for (size_t i = 0; i < count; i++) {
    memset(&out[i], 0, sizeof(mlg_Field));
    out[i].type = cfg->fields[i].type;
    out[i].display_style = cfg->fields[i].display_style;
    out[i].scale = cfg->fields[i].scale;
    out[i].transform = cfg->fields[i].offset;
    out[i].digits = cfg->fields[i].digits;
    strncpy(out[i].name, cfg->fields[i].name, MLG_FIELD_NAME_SIZE - 1);
    strncpy(out[i].units, cfg->fields[i].units, MLG_FIELD_UNITS_SIZE - 1);
    strncpy(out[i].category, cfg->fields[i].category, MLG_FIELD_CATEGORY_SIZE - 1);
  }
}

void can_map_reset_updated(can_FieldValues* fv) {
  fv->updated = 0;
}
