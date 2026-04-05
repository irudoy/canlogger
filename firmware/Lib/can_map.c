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
// Returns 0 on success, -1 if out of bounds
static int extract_value(const uint8_t* can_data, uint8_t dlc,
                          uint8_t start_byte, uint8_t bit_length,
                          uint8_t is_big_endian, uint8_t* dest) {
  size_t byte_count = bit_length / 8;
  if (byte_count == 0) byte_count = 1;

  if (start_byte + byte_count > dlc) return -1; // beyond actual data

  if (is_big_endian) {
    memcpy(dest, can_data + start_byte, byte_count);
  } else {
    for (size_t i = 0; i < byte_count; i++) {
      dest[i] = can_data[start_byte + byte_count - 1 - i];
    }
  }
  return 0;
}

// Linear interpolation on LUT. Returns display-unit value (e.g. °C, kPa).
static float lut_interpolate(const cfg_LutPoint* lut, uint8_t count, uint16_t input) {
  if (input <= lut[0].input) return (float)lut[0].output;
  if (input >= lut[count - 1].input) return (float)lut[count - 1].output;

  for (int i = 0; i < count - 1; i++) {
    if (input >= lut[i].input && input <= lut[i + 1].input) {
      float frac = (float)(input - lut[i].input) / (float)(lut[i + 1].input - lut[i].input);
      return (float)lut[i].output + frac * (float)(lut[i + 1].output - lut[i].output);
    }
  }
  return (float)lut[count - 1].output;
}

// Write a value into the shadow buffer as big-endian for the given MLG type.
static void write_value(uint8_t* dest, uint8_t type, float display_val, float scale, float offset) {
  // Inverse of MLG display formula: rawValue = displayValue / scale - offset
  float raw = display_val / scale - offset;
  int32_t ival = (int32_t)(raw >= 0 ? raw + 0.5f : raw - 0.5f); // round

  switch (type) {
    case 0: // U08
      dest[0] = (uint8_t)ival;
      break;
    case 1: // S08
      dest[0] = (uint8_t)(int8_t)ival;
      break;
    case 2: // U16
      dest[0] = (uint8_t)(ival >> 8);
      dest[1] = (uint8_t)ival;
      break;
    case 3: // S16
      dest[0] = (uint8_t)(ival >> 8);
      dest[1] = (uint8_t)ival;
      break;
    default:
      break;
  }
}

int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame) {
  int updated = 0;
  size_t offset = 0;

  for (int i = 0; i < cfg->num_fields; i++) {
    size_t field_size = mlg_field_data_size((mlg_FieldType)cfg->fields[i].type);

    if (cfg->fields[i].can_id == frame->id) {
      if (extract_value(frame->data, frame->dlc, cfg->fields[i].start_byte,
                        cfg->fields[i].bit_length, cfg->fields[i].is_big_endian,
                        fv->values + offset) == 0) {
        // Apply LUT if present
        if (cfg->fields[i].lut_count >= 2) {
          // Read extracted big-endian value as uint16
          uint16_t raw_input = (fv->values[offset] << 8) | fv->values[offset + 1];
          float display_val = lut_interpolate(cfg->fields[i].lut, cfg->fields[i].lut_count, raw_input);
          write_value(fv->values + offset, cfg->fields[i].type,
                      display_val, cfg->fields[i].scale, cfg->fields[i].offset);
        }
        updated++;
      }
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
