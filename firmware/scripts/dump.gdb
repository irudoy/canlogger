printf "=== State ===\n"
printf "error=%d shutdown=%d\n", error_state, lw_shutdown
printf "file=%s blocks=%d\n", log_file_name, block_counter

printf "=== Config ===\n"
printf "interval=%d bitrate=%d fields=%d can_ids=%d\n", config.log_interval_ms, config.can_bitrate, config.num_fields, config.num_can_ids

printf "=== CAN Registers ===\n"
printf "ESR=0x%08X RF0R=0x%08X BTR=0x%08X\n", *(uint32_t*)0x40006418, *(uint32_t*)0x4000640C, *(uint32_t*)0x4000641C
printf "IER=0x%08X NVIC_ISER0=0x%08X\n", *(uint32_t*)0x40006414, *(uint32_t*)0xE000E100

printf "=== Ring Buffer ===\n"
printf "head=%d tail=%d\n", can_rx_buf.head, can_rx_buf.tail
printf "[0] id=0x%X dlc=%d data=%02X %02X %02X %02X %02X %02X %02X %02X\n", can_rx_buf.frames[0].id, can_rx_buf.frames[0].dlc, can_rx_buf.frames[0].data[0], can_rx_buf.frames[0].data[1], can_rx_buf.frames[0].data[2], can_rx_buf.frames[0].data[3], can_rx_buf.frames[0].data[4], can_rx_buf.frames[0].data[5], can_rx_buf.frames[0].data[6], can_rx_buf.frames[0].data[7]
printf "[1] id=0x%X dlc=%d data=%02X %02X %02X %02X %02X %02X %02X %02X\n", can_rx_buf.frames[1].id, can_rx_buf.frames[1].dlc, can_rx_buf.frames[1].data[0], can_rx_buf.frames[1].data[1], can_rx_buf.frames[1].data[2], can_rx_buf.frames[1].data[3], can_rx_buf.frames[1].data[4], can_rx_buf.frames[1].data[5], can_rx_buf.frames[1].data[6], can_rx_buf.frames[1].data[7]
printf "[2] id=0x%X dlc=%d data=%02X %02X %02X %02X %02X %02X %02X %02X\n", can_rx_buf.frames[2].id, can_rx_buf.frames[2].dlc, can_rx_buf.frames[2].data[0], can_rx_buf.frames[2].data[1], can_rx_buf.frames[2].data[2], can_rx_buf.frames[2].data[3], can_rx_buf.frames[2].data[4], can_rx_buf.frames[2].data[5], can_rx_buf.frames[2].data[6], can_rx_buf.frames[2].data[7]

printf "=== Field Values ===\n"
printf "updated=%d reclen=%d\n", field_values.updated, field_values.record_length
printf "values: %02X %02X %02X %02X %02X %02X %02X\n", field_values.values[0], field_values.values[1], field_values.values[2], field_values.values[3], field_values.values[4], field_values.values[5], field_values.values[6]

printf "=== Backtrace ===\n"
bt 3
monitor resume
