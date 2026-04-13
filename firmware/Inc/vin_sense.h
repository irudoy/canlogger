#ifndef VIN_SENSE_H
#define VIN_SENSE_H

#include <stdint.h>

/* Call periodically (~100 ms) from task_producer.
 * Sets lw_shutdown and turns off LED D2 when power loss detected.
 * Arms only after first reading above threshold (floating pin = no trigger). */
void vin_sense_poll(void);

/* Read current ADC value in mV (0–3300). Safe to call anytime after MX_ADC1_Init. */
uint32_t vin_sense_read_mv(void);

/* Last polled value in mV, for debug status output. */
extern uint32_t vin_sense_mv;

#endif
