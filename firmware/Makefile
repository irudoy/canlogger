# CubeIDE paths
CUBEIDE      := /Applications/STM32CubeIDE.app/Contents/MacOS/STM32CubeIDE
CUBE_PLUGINS := /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins
PROGRAMMER   := $(CUBE_PLUGINS)/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macosaarch64_1.0.0.202601242230/tools/bin/STM32_Programmer_CLI
GDB_SERVER   := $(CUBE_PLUGINS)/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.macosaarch64_1.0.0.202601242230/tools/bin/ST-LINK_gdbserver
GDB          := $(CUBE_PLUGINS)/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.macosaarch64_1.0.0.202602081740/tools/bin/arm-none-eabi-gdb

PROJECT      := canlogger
CONFIG       := Debug
PROJECT_DIR  := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
ELF          := $(PROJECT_DIR)/Debug/$(PROJECT).elf
WS           := /tmp/canlogger-ws
STLINK_SN    := 2C2404147116303030303032

# --- Build ---

.PHONY: build clean rtc test flash erase reset debug gdb-server

build: rtc
	$(CUBEIDE) --launcher.suppressErrors -nosplash \
		-application org.eclipse.cdt.managedbuilder.core.headlessbuild \
		-data $(WS) \
		-import $(PROJECT_DIR) \
		-build $(PROJECT)/$(CONFIG)

clean:
	$(CUBEIDE) --launcher.suppressErrors -nosplash \
		-application org.eclipse.cdt.managedbuilder.core.headlessbuild \
		-data $(WS) \
		-import $(PROJECT_DIR) \
		-cleanBuild $(PROJECT)/$(CONFIG)

rtc:
	python3 generate_rtc_time.py

# --- Test (host) ---

test:
	$(MAKE) -C test

# --- Flash & Reset ---

flash: build
	$(PROGRAMMER) -c port=SWD sn=$(STLINK_SN) -w $(ELF) -v -rst

erase:
	$(PROGRAMMER) -c port=SWD sn=$(STLINK_SN) -e all

reset:
	$(PROGRAMMER) -c port=SWD sn=$(STLINK_SN) -rst

# --- Debug (OpenOCD) ---

ocd-server:
	openocd -f interface/stlink.cfg -c "hla_serial $(STLINK_SN)" -f target/stm32f4x.cfg

ocd-debug: build
	$(GDB) $(ELF) \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "load"

ocd-status:
	$(GDB) $(ELF) -batch \
		-ex "target extended-remote :3333" \
		-ex "monitor reset run" \
		-ex "shell sleep 3" \
		-ex "monitor halt" \
		-ex "printf \"=== State ===\n\"" \
		-ex "printf \"init_ok=%d error=%d shutdown=%d\n\", init_ok, error_state, lw_shutdown" \
		-ex "printf \"file=%s blocks=%d\n\", log_file_name, block_counter" \
		-ex "printf \"=== Config ===\n\"" \
		-ex "printf \"interval=%d bitrate=%d fields=%d can_ids=%d\n\", config.log_interval_ms, config.can_bitrate, config.num_fields, config.num_can_ids" \
		-ex "printf \"=== CAN ===\n\"" \
		-ex "printf \"rb head=%d tail=%d\n\", can_rx_buf.head, can_rx_buf.tail" \
		-ex "printf \"ESR=0x%08X RF0R=0x%08X BTR=0x%08X IER=0x%08X\n\", *(uint32_t*)0x40006418, *(uint32_t*)0x4000640C, *(uint32_t*)0x4000641C, *(uint32_t*)0x40006414" \
		-ex "printf \"NVIC_ISER0=0x%08X\n\", *(uint32_t*)0xE000E100" \
		-ex "printf \"=== Field Values ===\n\"" \
		-ex "printf \"updated=%d values: %d %d %d %d %d %d %d\n\", field_values.updated, field_values.values[0], field_values.values[1], field_values.values[2], field_values.values[3], field_values.values[4], field_values.values[5], field_values.values[6]" \
		-ex "bt" \
		-ex "monitor resume"

ocd-dump:
	$(GDB) $(ELF) -batch \
		-ex "target extended-remote :3333" \
		-ex "monitor reset run" \
		-ex "shell sleep 5" \
		-ex "monitor halt" \
		-ex "source scripts/dump.gdb"

# --- Debug (ST-LINK GDB server, requires recent ST-Link firmware) ---

gdb-server:
	$(GDB_SERVER) -p 3333 -d -e -cp $(dir $(PROGRAMMER))

debug: build
	$(GDB) $(ELF) \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "load"
