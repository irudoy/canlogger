#ifndef CAN_MAP_H
#define CAN_MAP_H

#include "config.h"
#include "ring_buf.h"
#include "mlvlg.h"
#include "gps_nmea.h"

#define CAN_MAP_MAX_RECORD_SIZE 1024

typedef struct {
  uint8_t  values[CAN_MAP_MAX_RECORD_SIZE]; // shadow buffer, big-endian field values
  size_t   record_length;
  uint16_t num_fields;
  uint8_t  updated;
} can_FieldValues;

// Initialize shadow buffer from config. Returns 0 on success.
int can_map_init(can_FieldValues* fv, const cfg_Config* cfg);

// Process a CAN frame: extract values and update shadow buffer.
// Returns number of fields updated.
int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame);

// Write all GPS-sourced field slots (cfg_Field.gps_source != CFG_GPS_SRC_NONE)
// from the current GPS state. Call under the same mutex that guards the
// shadow buffer. Returns number of fields written.
int can_map_update_gps(can_FieldValues* fv, const cfg_Config* cfg, const gps_State* gs);

// Build mlg_Field array from config (for writing MLG header).
void can_map_build_mlg_fields(const cfg_Config* cfg, mlg_Field* out, size_t max_fields);

// Reset updated flag.
void can_map_reset_updated(can_FieldValues* fv);

#endif // CAN_MAP_H
