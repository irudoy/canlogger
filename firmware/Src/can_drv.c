#include "can_drv.h"
#include "main.h"
#include "stm32f4xx_hal.h"

extern CAN_HandleTypeDef hcan1;

static ring_Buffer* rx_ring_buf = NULL;

// APB1 clock = 42 MHz. Use TQ=21 (BS1=15, BS2=5) for all standard bitrates.
// Prescaler = APB1 / (bitrate * TQ)
static int configure_timing(uint32_t bitrate) {
  uint32_t apb1_freq = 42000000;
  uint32_t tq = 21; // 1 + BS1(15) + BS2(5)

  uint32_t prescaler = apb1_freq / (bitrate * tq);
  if (prescaler == 0 || prescaler > 1024) return -1;

  // Verify exact match
  if (apb1_freq != prescaler * bitrate * tq) return -1;

  HAL_CAN_DeInit(&hcan1);

  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = prescaler;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_15TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_5TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;

  if (HAL_CAN_Init(&hcan1) != HAL_OK) return -1;
  return 0;
}

// Configure hardware filters for specific CAN IDs.
// STM32F4 CAN1 has 14 filter banks (0-13).
// If num_ids == 0 or > 14: accept-all filter.
// Otherwise: one filter per ID in ID-list mode.
static int configure_filters(const uint32_t* can_ids, uint16_t num_ids) {
  CAN_FilterTypeDef filter;

  if (num_ids == 0 || num_ids > 28) {
    // Accept-all: mask mode, all zeros
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
    return (HAL_CAN_ConfigFilter(&hcan1, &filter) == HAL_OK) ? 0 : -1;
  }

  // ID-list mode: 2 IDs per filter bank (32-bit scale), up to 14 banks = 28 IDs
  for (int i = 0; i < (num_ids + 1) / 2; i++) {
    uint32_t id1 = can_ids[i * 2];
    uint32_t id2 = (i * 2 + 1 < num_ids) ? can_ids[i * 2 + 1] : id1;

    filter.FilterBank = i;
    filter.FilterMode = CAN_FILTERMODE_IDLIST;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    // STM32 CAN filter register format: StdId is in bits [31:21]
    filter.FilterIdHigh = (id1 << 5) & 0xFFFF;
    filter.FilterIdLow = 0x0000;
    filter.FilterMaskIdHigh = (id2 << 5) & 0xFFFF;
    filter.FilterMaskIdLow = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) return -1;
  }

  // Deactivate unused banks
  for (int i = (num_ids + 1) / 2; i < 14; i++) {
    filter.FilterBank = i;
    filter.FilterActivation = DISABLE;
    HAL_CAN_ConfigFilter(&hcan1, &filter);
  }

  return 0;
}

int can_drv_init(ring_Buffer* rb, const cfg_Config* cfg) {
  rx_ring_buf = rb;

  if (configure_timing(cfg->can_bitrate) != 0) return -1;
  if (configure_filters(cfg->can_ids, cfg->num_can_ids) != 0) return -1;

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
