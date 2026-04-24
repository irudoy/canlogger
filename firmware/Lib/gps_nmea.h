#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include <stdint.h>
#include <stddef.h>

#define GPS_NMEA_MAX_LEN 96  // NMEA 0183 spec 82 chars incl. CRLF, +margin

// Decoded GPS state, accumulated from successive NMEA sentences.
// Fields are updated only by sentences that carry them; untouched fields
// keep their previous value. Callers should inspect has_* flags.
typedef struct {
  // UTC time (GGA or RMC)
  uint8_t  hour;         // 0-23
  uint8_t  minute;       // 0-59
  uint8_t  second;       // 0-59
  uint16_t millisecond;  // 0-999
  uint8_t  has_time;

  // UTC date (RMC only)
  uint16_t year;         // e.g. 2026
  uint8_t  month;        // 1-12
  uint8_t  day;          // 1-31
  uint8_t  has_date;

  // Position
  double  lat_deg;       // decimal degrees, south negative
  double  lon_deg;       // decimal degrees, west negative
  uint8_t has_position;

  // Altitude MSL, meters (GGA)
  float   altitude_m;
  uint8_t has_altitude;

  // Fix quality from GGA: 0 no fix, 1 GPS, 2 DGPS, 6 estimated
  uint8_t fix_quality;
  uint8_t satellites;    // number of sats used in solution (GGA)
  float   hdop;          // horizontal dilution of precision (GGA)
  uint8_t has_fix;       // 1 iff GGA.quality > 0 OR RMC.status == 'A'

  // Motion (RMC)
  float   speed_ms;      // meters per second (knots * 0.5144444)
  float   course_deg;    // degrees true, 0..359.99
  uint8_t has_motion;
} gps_State;

// Result of parse_sentence.
typedef enum {
  GPS_PARSE_OK = 0,        // recognized sentence, state updated
  GPS_PARSE_IGNORED,       // valid NMEA but not a sentence we decode (GSV/GLL/VTG/...)
  GPS_PARSE_BAD_CHECKSUM,  // '*' checksum mismatch
  GPS_PARSE_MALFORMED,     // missing '$', missing '*', bad structure
} gps_ParseResult;

// --- line assembler ---
//
// Accumulates bytes from UART into complete NMEA sentences. Call
// gps_lb_feed() with each received byte or chunk. When a full sentence
// terminates (CR or LF), it is null-terminated and passed out via `out`.
// Overlong sentences (> GPS_NMEA_MAX_LEN) are dropped with an overflow
// flag, and the buffer resets at the next '$'.

typedef struct {
  char    buf[GPS_NMEA_MAX_LEN];
  uint8_t len;
  uint8_t in_sentence;   // 1 after '$' was seen, 0 otherwise
  uint8_t overflow;      // latched when a sentence exceeded the buffer
} gps_LineBuffer;

void gps_lb_init(gps_LineBuffer* lb);

// Feed one byte. If a complete sentence is assembled, *out is set to
// the null-terminated sentence (pointing into lb->buf) and 1 is
// returned; otherwise 0 is returned. Caller must parse before the
// next call — the buffer is reused.
int gps_lb_feed_byte(gps_LineBuffer* lb, uint8_t byte, const char** out);

void gps_state_init(gps_State* s);

// Parse one NMEA sentence. Input must NOT include trailing CR/LF.
// Input may optionally include them — they are tolerated.
// Returns one of gps_ParseResult values.
gps_ParseResult gps_parse_sentence(gps_State* s, const char* sentence);

// Compute XOR checksum of NMEA payload (between '$' and '*').
// Returns 0..0xFF.
uint8_t gps_nmea_checksum(const char* payload_start, const char* payload_end);

#endif  // GPS_NMEA_H
