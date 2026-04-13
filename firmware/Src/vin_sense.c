#include "vin_sense.h"
#include "main.h"
#include "stm32f4xx_hal.h"

#define VIN_SENSE_SAMPLES   4
#define VIN_SENSE_DEBOUNCE  3

extern ADC_HandleTypeDef hadc1;
extern volatile uint8_t lw_shutdown;
extern volatile uint8_t vin_triggered_shutdown;  // distinguishes VIN outage from CDC `stop`

uint32_t vin_sense_mv;

uint32_t vin_sense_read_mv(void)
{
  uint32_t sum = 0;
  for (int i = 0; i < VIN_SENSE_SAMPLES; i++) {
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    sum += HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
  }
  return (sum * 3300) / (4095 * VIN_SENSE_SAMPLES);
}

void vin_sense_poll(void)
{
  static int armed = 0;
  static int low_count = 0;

  vin_sense_mv = vin_sense_read_mv();

  if (!armed) {
    if (vin_sense_mv >= VIN_SENSE_THRESHOLD_MV)
      armed = 1;
    return;
  }

  if (vin_sense_mv < VIN_SENSE_THRESHOLD_MV) {
    if (++low_count >= VIN_SENSE_DEBOUNCE) {
      vin_triggered_shutdown = 1;  // mark before lw_shutdown so observer never sees stale flag
      lw_shutdown = 1;
      HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
    }
  } else {
    low_count = 0;
  }
}
