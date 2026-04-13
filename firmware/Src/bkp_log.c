#include "bkp_log.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* Layout:
 *   DR1: session counter (increments on boot)
 *   DR2: session at which last fault occurred
 *   DR3: (at << 16) | res — packed fault info
 */
#define BKP_SESSION      RTC_BKP_DR1
#define BKP_FAULT_SESS   RTC_BKP_DR2
#define BKP_FAULT_INFO   RTC_BKP_DR3

extern RTC_HandleTypeDef hrtc;

void bkp_log_init(void) {
  uint32_t s = HAL_RTCEx_BKUPRead(&hrtc, BKP_SESSION);
  HAL_RTCEx_BKUPWrite(&hrtc, BKP_SESSION, s + 1);
}

uint32_t bkp_log_session(void) {
  return HAL_RTCEx_BKUPRead(&hrtc, BKP_SESSION);
}

void bkp_log_fault(uint8_t res, bkp_at_t at) {
  uint32_t info = ((uint32_t)at << 16) | res;
  // Write session first: if power drops between the two writes, info stays
  // bound to the session that recorded it (worst case: stale info, new sess;
  // reader still sees a valid entry from an earlier session).
  HAL_RTCEx_BKUPWrite(&hrtc, BKP_FAULT_SESS, bkp_log_session());
  HAL_RTCEx_BKUPWrite(&hrtc, BKP_FAULT_INFO, info);
}

int bkp_log_get_last(uint8_t* res, uint8_t* at, uint32_t* fault_session) {
  uint32_t info = HAL_RTCEx_BKUPRead(&hrtc, BKP_FAULT_INFO);
  uint32_t sess = HAL_RTCEx_BKUPRead(&hrtc, BKP_FAULT_SESS);
  if (sess == 0) return 0;  // never recorded
  if (res) *res = info & 0xFF;
  if (at) *at = (info >> 16) & 0xFF;
  if (fault_session) *fault_session = sess;
  return 1;
}

static const char* const at_names[] = {
  [BKP_AT_NONE]      = "none",
  [BKP_AT_MOUNT]     = "mount",
  [BKP_AT_CFG_OPEN]  = "cfg_open",
  [BKP_AT_CFG_PARSE] = "cfg_parse",
  [BKP_AT_CAN_INIT]  = "can_init",
  [BKP_AT_CREATE]    = "create",
  [BKP_AT_HEADER]    = "header",
  [BKP_AT_WRITE]     = "write",
  [BKP_AT_SYNC]      = "sync",
  [BKP_AT_ROTATE]    = "rotate",
  [BKP_AT_RECOVERY]  = "recovery",
  [BKP_AT_TEST]      = "test",
};

const char* bkp_at_name(uint8_t at) {
  if (at >= sizeof(at_names) / sizeof(at_names[0])) return "?";
  return at_names[at] ? at_names[at] : "?";
}

bkp_at_t bkp_at_from_name(const char* name) {
  if (!name) return BKP_AT_NONE;
  for (size_t i = 0; i < sizeof(at_names) / sizeof(at_names[0]); i++) {
    if (at_names[i] && !strcmp(name, at_names[i])) return (bkp_at_t)i;
  }
  return BKP_AT_NONE;
}
