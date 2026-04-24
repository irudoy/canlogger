#include "gps_nmea.h"

#include <string.h>
#include <stdlib.h>

// ---------- helpers ----------

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

uint8_t gps_nmea_checksum(const char* payload_start, const char* payload_end) {
  uint8_t cs = 0;
  for (const char* p = payload_start; p < payload_end; ++p) cs ^= (uint8_t)*p;
  return cs;
}

// Split sentence by ','. Writes pointers to each field start into `fields`
// and replaces commas with '\0' in `buf`. Returns field count.
// Note: mutates `buf`.
static size_t split_fields(char* buf, const char** fields, size_t max_fields) {
  size_t n = 0;
  fields[n++] = buf;
  for (char* p = buf; *p && n < max_fields; ++p) {
    if (*p == ',') {
      *p = '\0';
      fields[n++] = p + 1;
    }
  }
  return n;
}

// Parse decimal integer from N chars. Returns value or -1 on empty/invalid.
static int parse_int_n(const char* s, size_t n) {
  if (n == 0 || !s[0]) return -1;
  int v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

// Parse fractional part after decimal point into milliseconds (0..999).
// Stops at non-digit. Pads to 3 digits.
static uint16_t parse_millis(const char* s) {
  uint16_t ms = 0;
  int digits = 0;
  while (*s && *s >= '0' && *s <= '9' && digits < 3) {
    ms = ms * 10 + (*s - '0');
    ++s; ++digits;
  }
  while (digits < 3) { ms *= 10; ++digits; }
  return ms;
}

// Parse NMEA time field hhmmss[.sss]. Returns 1 on success, 0 on empty/invalid.
static int parse_time(const char* f, uint8_t* h, uint8_t* m, uint8_t* s, uint16_t* ms) {
  if (!f || !f[0]) return 0;
  if (strlen(f) < 6) return 0;
  int hh = parse_int_n(f, 2);
  int mm = parse_int_n(f + 2, 2);
  int ss = parse_int_n(f + 4, 2);
  if (hh < 0 || mm < 0 || ss < 0) return 0;
  if (hh > 23 || mm > 59 || ss > 60) return 0;  // 60 = leap second
  *h = (uint8_t)hh; *m = (uint8_t)mm; *s = (uint8_t)ss;
  *ms = (f[6] == '.') ? parse_millis(f + 7) : 0;
  return 1;
}

// Parse NMEA date field ddmmyy. Returns 1 on success.
static int parse_date(const char* f, uint16_t* year, uint8_t* month, uint8_t* day) {
  if (!f || strlen(f) != 6) return 0;
  int dd = parse_int_n(f, 2);
  int mm = parse_int_n(f + 2, 2);
  int yy = parse_int_n(f + 4, 2);
  if (dd < 1 || dd > 31 || mm < 1 || mm > 12 || yy < 0) return 0;
  *day = (uint8_t)dd; *month = (uint8_t)mm;
  // NMEA uses 2-digit year. Pivot at 80: 80..99 => 1980..1999, 00..79 => 2000..2079.
  *year = (uint16_t)(yy >= 80 ? 1900 + yy : 2000 + yy);
  return 1;
}

// Parse ddmm.mmmm / dddmm.mmmm to decimal degrees.
// deg_digits = 2 for latitude, 3 for longitude.
// Returns 1 on success, 0 if empty/invalid.
static int parse_latlon(const char* f, int deg_digits, double* out) {
  if (!f || !f[0]) return 0;
  // Field must have at least deg_digits+2 before the decimal point.
  size_t len = strlen(f);
  if (len < (size_t)(deg_digits + 2)) return 0;
  int deg = parse_int_n(f, deg_digits);
  if (deg < 0) return 0;
  // Minutes: up to the end of string or next ','.
  // atof handles trailing garbage by stopping at non-numeric.
  double minutes = atof(f + deg_digits);
  if (minutes < 0.0 || minutes >= 60.0) return 0;
  *out = (double)deg + minutes / 60.0;
  return 1;
}

// ---------- line assembler ----------

void gps_lb_init(gps_LineBuffer* lb) {
  memset(lb, 0, sizeof(*lb));
}

int gps_lb_feed_byte(gps_LineBuffer* lb, uint8_t byte, const char** out) {
  // '$' always starts a fresh sentence, regardless of prior state.
  if (byte == '$') {
    lb->len = 0;
    lb->buf[lb->len++] = (char)byte;
    lb->in_sentence = 1;
    return 0;
  }
  if (!lb->in_sentence) return 0;

  // CR / LF terminates a sentence.
  if (byte == '\r' || byte == '\n') {
    if (lb->len == 0) return 0;
    if (lb->len >= GPS_NMEA_MAX_LEN) {
      lb->overflow = 1;
      lb->in_sentence = 0;
      lb->len = 0;
      return 0;
    }
    lb->buf[lb->len] = '\0';
    *out = lb->buf;
    lb->in_sentence = 0;
    return 1;
  }

  // Ordinary character.
  if (lb->len >= GPS_NMEA_MAX_LEN - 1) {
    lb->overflow = 1;
    lb->in_sentence = 0;
    lb->len = 0;
    return 0;
  }
  lb->buf[lb->len++] = (char)byte;
  return 0;
}

// ---------- API ----------

void gps_state_init(gps_State* s) {
  memset(s, 0, sizeof(*s));
}

gps_ParseResult gps_parse_sentence(gps_State* s, const char* sentence) {
  if (!sentence || sentence[0] != '$') return GPS_PARSE_MALFORMED;

  // Copy into local buffer so we can mutate.
  char buf[GPS_NMEA_MAX_LEN];
  size_t in_len = strnlen(sentence, GPS_NMEA_MAX_LEN);
  if (in_len >= GPS_NMEA_MAX_LEN) return GPS_PARSE_MALFORMED;
  memcpy(buf, sentence, in_len + 1);

  // Trim trailing CR/LF if present.
  while (in_len > 0 && (buf[in_len - 1] == '\r' || buf[in_len - 1] == '\n')) {
    buf[--in_len] = '\0';
  }

  // Locate '*' checksum delimiter.
  char* star = strchr(buf, '*');
  if (!star) return GPS_PARSE_MALFORMED;
  if (strlen(star + 1) < 2) return GPS_PARSE_MALFORMED;

  int hi = hex_nibble(star[1]);
  int lo = hex_nibble(star[2]);
  if (hi < 0 || lo < 0) return GPS_PARSE_MALFORMED;
  uint8_t expected = (uint8_t)((hi << 4) | lo);

  // Payload is between '$' (exclusive) and '*' (exclusive).
  uint8_t actual = gps_nmea_checksum(buf + 1, star);
  if (actual != expected) return GPS_PARSE_BAD_CHECKSUM;

  // Terminate at '*' so split_fields sees a clean payload.
  *star = '\0';

  // Split into comma-separated fields.
  const char* fields[24] = {0};
  size_t nf = split_fields(buf, fields, 24);
  if (nf < 1) return GPS_PARSE_MALFORMED;

  // Talker ID is first 2 chars after '$', sentence type is chars 3..5.
  // Accepted talker IDs: GP (GPS), GN (multi-GNSS), GL (GLONASS), GA (Galileo), BD/GB (BeiDou).
  // For now decode only GGA and RMC regardless of talker.
  const char* tag = fields[0] + 1;  // skip '$'
  if (strlen(tag) < 5) return GPS_PARSE_MALFORMED;
  const char* type = tag + 2;

  if (memcmp(type, "GGA", 3) == 0) {
    // $xxGGA,time,lat,ns,lon,ew,fix,sats,hdop,alt,altu,geoid,geoidu,age,stn
    if (nf < 10) return GPS_PARSE_MALFORMED;
    uint8_t hh, mm, ss; uint16_t ms;
    if (parse_time(fields[1], &hh, &mm, &ss, &ms)) {
      s->hour = hh; s->minute = mm; s->second = ss; s->millisecond = ms;
      s->has_time = 1;
    }
    int fix = parse_int_n(fields[6], strlen(fields[6]));
    if (fix < 0) fix = 0;
    s->fix_quality = (uint8_t)fix;
    // If we have a fix, update position/alt/sats/hdop.
    if (fix > 0) {
      double lat, lon;
      if (parse_latlon(fields[2], 2, &lat) && parse_latlon(fields[4], 3, &lon)) {
        if (fields[3][0] == 'S') lat = -lat;
        if (fields[5][0] == 'W') lon = -lon;
        s->lat_deg = lat; s->lon_deg = lon; s->has_position = 1;
      }
      int sats = parse_int_n(fields[7], strlen(fields[7]));
      if (sats >= 0) s->satellites = (uint8_t)sats;
      if (fields[8][0]) s->hdop = (float)atof(fields[8]);
      if (fields[9][0]) { s->altitude_m = (float)atof(fields[9]); s->has_altitude = 1; }
      s->has_fix = 1;
    } else {
      s->has_fix = 0;
      s->satellites = 0;
    }
    return GPS_PARSE_OK;
  }

  if (memcmp(type, "RMC", 3) == 0) {
    // $xxRMC,time,status,lat,ns,lon,ew,speedKn,courseDeg,date,magvar,ew,mode
    if (nf < 10) return GPS_PARSE_MALFORMED;
    uint8_t hh, mm, ss; uint16_t ms;
    if (parse_time(fields[1], &hh, &mm, &ss, &ms)) {
      s->hour = hh; s->minute = mm; s->second = ss; s->millisecond = ms;
      s->has_time = 1;
    }
    char status = fields[2][0];
    uint8_t active = (status == 'A') ? 1 : 0;
    if (active) {
      double lat, lon;
      if (parse_latlon(fields[3], 2, &lat) && parse_latlon(fields[5], 3, &lon)) {
        if (fields[4][0] == 'S') lat = -lat;
        if (fields[6][0] == 'W') lon = -lon;
        s->lat_deg = lat; s->lon_deg = lon; s->has_position = 1;
      }
      if (fields[7][0]) {
        float knots = (float)atof(fields[7]);
        s->speed_ms = knots * 0.5144444f;
        s->has_motion = 1;
      }
      if (fields[8][0]) s->course_deg = (float)atof(fields[8]);
      uint16_t y; uint8_t mo, d;
      if (parse_date(fields[9], &y, &mo, &d)) {
        s->year = y; s->month = mo; s->day = d; s->has_date = 1;
      }
      s->has_fix = 1;
    } else {
      s->has_fix = 0;
    }
    return GPS_PARSE_OK;
  }

  return GPS_PARSE_IGNORED;
}
