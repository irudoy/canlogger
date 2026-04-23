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
                          uint8_t start_byte, uint8_t start_bit,
                          uint8_t bit_length,
                          uint8_t is_big_endian, uint8_t* dest) {
  // Sub-byte extraction: single source byte, shift+mask into dest[0].
  if (bit_length < 8) {
    if (start_byte >= dlc) return -1;
    uint8_t mask = (uint8_t)((1u << bit_length) - 1);
    dest[0] = (can_data[start_byte] >> start_bit) & mask;
    return 0;
  }

  size_t byte_count = bit_length / 8;
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

// Read current shadow value as raw integer (big-endian), then convert to display units.
// Needed so plausibility checks compare in the same units as config thresholds.
static float raw_to_display(const uint8_t* src, uint8_t type, float scale, float offset) {
  int32_t ival = 0;
  switch (type) {
    case 0: ival = src[0]; break;
    case 1: ival = (int8_t)src[0]; break;
    case 2: ival = ((uint16_t)src[0] << 8) | src[1]; break;
    case 3: {
      int16_t s = (int16_t)(((uint16_t)src[0] << 8) | src[1]);
      ival = s;
      break;
    }
    default: return 0.0f;
  }
  return ((float)ival + offset) * scale;
}

// Applies preset-specific sensor-fault detectors that cannot be expressed as a
// simple valid_min/valid_max range. Returns 1 if preset rejects the value.
static int preset_rejects(const cfg_Field* f, const uint8_t* extracted) {
  if (f->preset == CFG_PRESET_AEM_UEGO && f->bit_length == 16) {
    // AEM X-Series UEGO streams 0xFFFF for Warmup/Free-Air-Cal/Sensor-Fault
    // on the Lambda/AFR word. On decel fuel-cut the gauge also returns the
    // maxed-out reading; either way 0xFFFF is not real combustion data.
    uint16_t raw = ((uint16_t)extracted[0] << 8) | extracted[1];
    return raw == 0xFFFF;
  }
  return 0;
}

// Applies invalid_strategy. dest points to the field's slot in the shadow
// buffer; extracted holds the freshly decoded bytes that might be rejected.
// Returns 1 if shadow buffer was updated, 0 if it stayed unchanged.
static int apply_invalid(const cfg_Field* f, uint8_t* dest, const uint8_t* extracted) {
  switch (f->invalid_strategy) {
    case CFG_INVALID_SKIP:
    case CFG_INVALID_LAST_GOOD:
      (void)extracted;
      return 0; // shadow stays at last good value
    case CFG_INVALID_CLAMP: {
      // Pick whichever bound the extracted value overshoots.
      float ext_disp = raw_to_display(extracted, f->type, f->scale, f->offset);
      float clamped = ext_disp;
      if (f->has_valid_min && clamped < f->valid_min) clamped = f->valid_min;
      if (f->has_valid_max && clamped > f->valid_max) clamped = f->valid_max;
      write_value(dest, f->type, clamped, f->scale, f->offset);
      return 1;
    }
    default:
      return 0;
  }
}

int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame) {
  int updated = 0;
  size_t offset = 0;

  for (int i = 0; i < cfg->num_fields; i++) {
    const cfg_Field* f = &cfg->fields[i];
    size_t field_size = mlg_field_data_size((mlg_FieldType)f->type);

    if (f->can_id == frame->id) {
      // Extract into a scratch buffer first — lets us reject without
      // already having clobbered the shadow's last-good value.
      uint8_t extracted[8] = {0};
      if (extract_value(frame->data, frame->dlc, f->start_byte,
                        f->start_bit, f->bit_length,
                        f->is_big_endian,
                        extracted) == 0) {
        int rejected = preset_rejects(f, extracted);

        // Apply LUT to produce the display-unit value, then pack back into
        // the scratch buffer in MLG storage form so plausibility and the
        // shadow copy share one code path.
        if (!rejected && f->lut_count >= 2) {
          uint16_t raw_input = ((uint16_t)extracted[0] << 8) | extracted[1];
          float display_val = lut_interpolate(f->lut, f->lut_count, raw_input);
          write_value(extracted, f->type, display_val, f->scale, f->offset);
        }

        if (!rejected && (f->has_valid_min || f->has_valid_max)) {
          float disp = raw_to_display(extracted, f->type, f->scale, f->offset);
          if (f->has_valid_min && disp < f->valid_min) rejected = 1;
          if (f->has_valid_max && disp > f->valid_max) rejected = 1;
        }

        if (rejected) {
          if (apply_invalid(f, fv->values + offset, extracted)) updated++;
        } else {
          memcpy(fv->values + offset, extracted, field_size);
          updated++;
        }
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
