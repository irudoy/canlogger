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
	$(PROGRAMMER) -c port=SWD -w $(ELF) -v -rst

erase:
	$(PROGRAMMER) -c port=SWD -e all

reset:
	$(PROGRAMMER) -c port=SWD -rst

# --- Debug (OpenOCD) ---

ocd-server:
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

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
		-ex "print error_state" \
		-ex "print lw_shutdown" \
		-ex "print (char*)log_file_name" \
		-ex "print file_counter" \
		-ex "print error_count" \
		-ex "print block_counter" \
		-ex "bt" \
		-ex "monitor resume"

# --- Debug (ST-LINK GDB server, requires recent ST-Link firmware) ---

gdb-server:
	$(GDB_SERVER) -p 3333 -d -e -cp $(dir $(PROGRAMMER))

debug: build
	$(GDB) $(ELF) \
		-ex "target extended-remote :3333" \
		-ex "monitor reset halt" \
		-ex "load"
