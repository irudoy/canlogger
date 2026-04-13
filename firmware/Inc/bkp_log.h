#ifndef BKP_LOG_H
#define BKP_LOG_H

#include <stdint.h>

/* Persistent fault log in RTC backup registers (DR1-DR3).
 * Survives reset and power loss as long as VBAT (CR1220) is alive.
 * Used for faults that can't be written to SD — e.g. SD mount failure. */

typedef enum {
  BKP_AT_NONE = 0,
  BKP_AT_MOUNT,
  BKP_AT_CFG_OPEN,
  BKP_AT_CFG_PARSE,
  BKP_AT_CAN_INIT,
  BKP_AT_CREATE,
  BKP_AT_HEADER,
  BKP_AT_WRITE,
  BKP_AT_SYNC,
  BKP_AT_ROTATE,
  BKP_AT_RECOVERY,
  BKP_AT_TEST,
} bkp_at_t;

/* Call once after HAL_RTC_Init — increments session counter. */
void bkp_log_init(void);

/* Current session counter (boots since VBAT last lost). */
uint32_t bkp_log_session(void);

/* Record a fault. res is FRESULT, at is bkp_at_t. */
void bkp_log_fault(uint8_t res, bkp_at_t at);

/* Read last recorded fault. Returns 0 if no fault was ever recorded. */
int bkp_log_get_last(uint8_t* res, uint8_t* at, uint32_t* fault_session);

/* Convert bkp_at_t to short string. */
const char* bkp_at_name(uint8_t at);

/* Convert short string back to bkp_at_t. Returns BKP_AT_NONE if unknown. */
bkp_at_t bkp_at_from_name(const char* name);

#endif
