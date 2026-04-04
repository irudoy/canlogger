#include "can_drv.h"
#include "main.h"
#include "stm32f4xx_hal.h"

extern CAN_HandleTypeDef hcan1;

static ring_Buffer* rx_ring_buf = NULL;

static void configure_filter(void) {
  CAN_FilterTypeDef filter;
  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000;
  filter.FilterIdLow = 0x0000;
  filter.FilterMaskIdHigh = 0x0000;
  filter.FilterMaskIdLow = 0x0000;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = 14;
  HAL_CAN_ConfigFilter(&hcan1, &filter);
}

int can_drv_init(ring_Buffer* rb) {
  rx_ring_buf = rb;
  configure_filter();
  return 0;
}

int can_drv_start(void) {
  if (HAL_CAN_Start(&hcan1) != HAL_OK) return -1;
  if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return -1;
  return 0;
}

void can_drv_stop(void) {
  HAL_CAN_DeactivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
  HAL_CAN_Stop(&hcan1);
}

// Called by HAL from CAN1_RX0_IRQHandler
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
  if (!rx_ring_buf) return;

  CAN_RxHeaderTypeDef rx_header;
  can_Frame frame;

  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, frame.data) == HAL_OK) {
    frame.id = (rx_header.IDE == CAN_ID_STD) ? rx_header.StdId : rx_header.ExtId;
    frame.dlc = rx_header.DLC;
    ring_buf_push(rx_ring_buf, &frame);
  }
}
