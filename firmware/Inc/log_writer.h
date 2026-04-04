#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include "fatfs.h"
#include "config.h"
#include "can_map.h"

// Initialize: mount SD, read config, write MLG header.
// Returns 0 on success, -1 on error (check lw_get_error).
int lw_init(cfg_Config* cfg_out, can_FieldValues* fv_out);

// Write a data block from shadow buffer if log interval elapsed.
FRESULT lw_tick(const can_FieldValues* fv, uint32_t log_interval_ms);

// Flush and close.
void lw_stop(void);

// LED update (call every loop iteration).
void lw_update_leds(void);

// Check error state.
int lw_is_error(void);

#endif // LOG_WRITER_H
