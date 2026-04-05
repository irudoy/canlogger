#include "config.h"
#include <string.h>

typedef enum {
  SEC_NONE,
  SEC_LOGGER,
  SEC_FIELD
} Section;

static void trim_right(char* s) {
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
    s[--len] = '\0';
  }
}

static const char* skip_spaces(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static uint8_t parse_type(const char* val) {
  if (strcmp(val, "U08") == 0) return 0;
  if (strcmp(val, "S08") == 0) return 1;
  if (strcmp(val, "U16") == 0) return 2;
  if (strcmp(val, "S16") == 0) return 3;
  if (strcmp(val, "U32") == 0) return 4;
  if (strcmp(val, "S32") == 0) return 5;
  if (strcmp(val, "S64") == 0) return 6;
  if (strcmp(val, "F32") == 0) return 7;
  return 0xFF;
}

static uint32_t parse_uint32(const char* val) {
  uint32_t result = 0;
  if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
    val += 2;
    while (*val) {
      char c = *val++;
      uint8_t digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = 10 + c - 'a';
      else if (c >= 'A' && c <= 'F') digit = 10 + c - 'A';
      else break;
      result = (result << 4) | digit;
    }
  } else {
    while (*val >= '0' && *val <= '9') {
      result = result * 10 + (*val++ - '0');
    }
  }
  return result;
}

static float parse_float(const char* s) {
  float sign = 1.0f;
  if (*s == '-') { sign = -1.0f; s++; }
  else if (*s == '+') { s++; }

  float result = 0.0f;
  while (*s >= '0' && *s <= '9') {
    result = result * 10.0f + (*s - '0');
    s++;
  }

  if (*s == '.') {
    s++;
    float frac = 0.1f;
    while (*s >= '0' && *s <= '9') {
      result += (*s - '0') * frac;
      frac *= 0.1f;
      s++;
    }
  }

  return sign * result;
}

static int parse_lut(const char* val, cfg_LutPoint* lut, uint8_t* count) {
  *count = 0;
  const char* p = val;
  while (*p && *count < CFG_LUT_MAX) {
    p = skip_spaces(p);
    if (!*p) break;
    // Parse input (uint16)
    uint16_t input = 0;
    while (*p >= '0' && *p <= '9') {
      input = input * 10 + (*p - '0');
      p++;
    }
    if (*p != ':') return -1;
    p++;
    // Parse output (int16, may be negative)
    int16_t sign = 1;
    if (*p == '-') { sign = -1; p++; }
    int16_t output = 0;
    while (*p >= '0' && *p <= '9') {
      output = output * 10 + (*p - '0');
      p++;
    }
    output *= sign;
    lut[*count].input = input;
    lut[*count].output = output;
    (*count)++;
    // Skip comma and spaces
    p = skip_spaces(p);
    if (*p == ',') p++;
  }
  return (*count >= 2) ? 0 : -1; // need at least 2 points
}

static void copy_str(char* dest, const char* src, size_t max_len) {
  strncpy(dest, src, max_len - 1);
  dest[max_len - 1] = '\0';
}

int cfg_parse(const char* text, size_t len, cfg_Config* out) {
  if (!text || !out) return CFG_ERR_MISSING;

  memset(out, 0, sizeof(cfg_Config));

  Section section = SEC_NONE;
  int logger_found = 0;
  int field_idx = -1;

  const char* p = text;
  const char* end = text + len;

  char line[256];

  while (p < end) {
    // Extract one line
    const char* eol = p;
    while (eol < end && *eol != '\n') eol++;

    size_t line_len = eol - p;
    if (line_len >= sizeof(line)) {
      p = eol + 1;
      continue; // skip lines longer than buffer
    }
    memcpy(line, p, line_len);
    line[line_len] = '\0';
    trim_right(line);

    p = eol + 1;

    // Skip empty lines and comments
    const char* s = skip_spaces(line);
    if (*s == '\0' || *s == '#' || *s == ';') continue;

    // Section header
    if (*s == '[') {
      const char* close = strchr(s, ']');
      if (!close) continue;

      char sec_name[32] = {0};
      size_t sec_len = close - s - 1;
      if (sec_len >= sizeof(sec_name)) sec_len = sizeof(sec_name) - 1;
      memcpy(sec_name, s + 1, sec_len);
      sec_name[sec_len] = '\0';

      if (strcmp(sec_name, "logger") == 0) {
        section = SEC_LOGGER;
        logger_found = 1;
      } else if (strcmp(sec_name, "field") == 0) {
        section = SEC_FIELD;
        field_idx++;
        if (field_idx >= CFG_MAX_FIELDS) {
          return CFG_ERR_OVERFLOW;
        }
        // Set defaults
        out->fields[field_idx].scale = 1.0f;
        out->fields[field_idx].offset = 0.0f;
        out->fields[field_idx].digits = 0;
        out->fields[field_idx].display_style = 0;
        out->fields[field_idx].is_big_endian = 0;
        out->fields[field_idx].category[0] = '\0';
        out->num_fields = field_idx + 1;
      }
      continue;
    }

    // Key = value
    char* eq = strchr(line, '=');
    if (!eq) continue;

    *eq = '\0';
    char* key = line;
    trim_right(key);
    const char* raw_key = skip_spaces(key);

    const char* val = skip_spaces(eq + 1);
    // Trim trailing from val (already done via line trim, but val might have leading space)
    char val_buf[128];
    copy_str(val_buf, val, sizeof(val_buf));
    trim_right(val_buf);

    if (section == SEC_LOGGER) {
      if (strcmp(raw_key, "interval_ms") == 0) {
        out->log_interval_ms = parse_uint32(val_buf);
      } else if (strcmp(raw_key, "can_bitrate") == 0) {
        out->can_bitrate = parse_uint32(val_buf);
      }
    } else if (section == SEC_FIELD && field_idx >= 0) {
      cfg_Field* f = &out->fields[field_idx];
      if (strcmp(raw_key, "can_id") == 0) {
        f->can_id = parse_uint32(val_buf);
      } else if (strcmp(raw_key, "name") == 0) {
        copy_str(f->name, val_buf, CFG_NAME_SIZE);
      } else if (strcmp(raw_key, "units") == 0) {
        copy_str(f->units, val_buf, CFG_UNITS_SIZE);
      } else if (strcmp(raw_key, "start_byte") == 0) {
        f->start_byte = (uint8_t)parse_uint32(val_buf);
      } else if (strcmp(raw_key, "bit_length") == 0) {
        f->bit_length = (uint8_t)parse_uint32(val_buf);
      } else if (strcmp(raw_key, "type") == 0) {
        uint8_t t = parse_type(val_buf);
        if (t == 0xFF) return CFG_ERR_VALUE;
        f->type = t;
      } else if (strcmp(raw_key, "scale") == 0) {
        f->scale = parse_float(val_buf);
      } else if (strcmp(raw_key, "offset") == 0) {
        f->offset = parse_float(val_buf);
      } else if (strcmp(raw_key, "digits") == 0) {
        f->digits = (int8_t)parse_uint32(val_buf);
      } else if (strcmp(raw_key, "display_style") == 0) {
        f->display_style = (uint8_t)parse_uint32(val_buf);
      } else if (strcmp(raw_key, "is_big_endian") == 0) {
        f->is_big_endian = (uint8_t)parse_uint32(val_buf);
      } else if (strcmp(raw_key, "category") == 0) {
        copy_str(f->category, val_buf, CFG_CAT_SIZE);
      } else if (strcmp(raw_key, "lut") == 0) {
        if (parse_lut(val_buf, f->lut, &f->lut_count) != 0) return CFG_ERR_VALUE;
      }
    }
  }

  if (!logger_found) return CFG_ERR_MISSING;

  // Default bitrate
  if (out->can_bitrate == 0) out->can_bitrate = 500000;

  // Collect unique CAN IDs from fields
  out->num_can_ids = 0;
  for (int i = 0; i < out->num_fields; i++) {
    int found = 0;
    for (int j = 0; j < out->num_can_ids; j++) {
      if (out->can_ids[j] == out->fields[i].can_id) { found = 1; break; }
    }
    if (!found && out->num_can_ids < CFG_MAX_CAN_IDS) {
      out->can_ids[out->num_can_ids++] = out->fields[i].can_id;
    }
  }

  // Validate fields
  for (int i = 0; i < out->num_fields; i++) {
    cfg_Field* f = &out->fields[i];
    if (f->bit_length == 0 || f->bit_length % 8 != 0) return CFG_ERR_VALUE;
    if (f->start_byte + f->bit_length / 8 > 8) return CFG_ERR_VALUE;
  }

  return CFG_OK;
}
