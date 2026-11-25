# OpenGD77 MD-9600 / RT-90 firmware build (SP7TEAM EMCOM fork)
# Mirrors MDUV380_firmware/Makefile; build configuration from .cproject MD9600_HW_V<n>

TARGET    := MD9600_FW
BUILD_DIR := build

# --- target hardware version ---------------------------------------------------
# The MD-9600 shipped with several PLL / IF chip sets; each needs its own build.
# This fork supports HW V5 (with GPS). Override for testing: make all HW=4
HW ?= 5
PLATFORM := MD9600
VARIANT  := HW_V$(HW)

PLATFORM_DEFS := -DPLATFORM_MD9600 -DMD9600_VERSION_$(HW)
OUT_NAME      ?= OpenGD77_MD9600_HW_V$(HW)

# --- toolchain ---------------------------------------------------------------
CROSS    := arm-none-eabi-
CC       := $(CROSS)gcc
AS       := $(CROSS)gcc -x assembler-with-cpp
LD       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size

# --- MCU / arch flags --------------------------------------------------------
MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# --- preprocessor ------------------------------------------------------------
DEFS := \
    -DUSE_HAL_DRIVER \
    -DSTM32F405xx \
    $(PLATFORM_DEFS)

# SEGGER RTT debug output (SWD debugger); release builds leave it out (no RTT RAM)
# DEFS += -DUSING_EXTERNAL_DEBUGGER

# Optional language pack (uncomment one to bake a default language)
# DEFS += -DLANGUAGE_BUILD_JAPANESE

# GITVERSION matches what STM32CubeIDE injects in .cproject
GITVERSION := $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null || echo UNKNOWN)
DEFS += -DGITVERSION=\"$(GITVERSION)\"

INCLUDES := \
    -I. \
    -ICore/Inc \
    -Iapplication/include \
    -IUSB_DEVICE/Target \
    -IUSB_DEVICE/App \
    -IMiddlewares/Third_Party/FreeRTOS/Source/include \
    -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
    -IMiddlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
    -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
    -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
    -IDrivers/CMSIS/Include \
    -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
    -IDrivers/STM32F4xx_HAL_Driver/Inc \
    -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy

# --- compile flags -----------------------------------------------------------
COMMON_FLAGS := $(MCU_FLAGS) -Os -g3 -ffunction-sections -fdata-sections -Wall \
                -Wno-format -Wno-format-truncation -Wno-stringop-overflow \
                -fstack-usage --specs=nano.specs

CFLAGS   := $(COMMON_FLAGS) $(DEFS) $(INCLUDES) -std=gnu11 -MMD -MP
ASFLAGS  := $(COMMON_FLAGS) $(DEFS) -MMD -MP -Wa,-I,$(BUILD_DIR)
LDSCRIPT := STM32F405VGTX_FLASH.ld
LDFLAGS  := $(MCU_FLAGS) -T$(LDSCRIPT) --specs=nosys.specs --specs=nano.specs \
            -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
            -Wl,--gc-sections -static \
            -Wl,--start-group -lc -lm -Wl,--end-group

# --- sources -----------------------------------------------------------------
# Source roots from .cproject sourceEntries
SRC_ROOTS := SeggerRTT application Core Middlewares Drivers USB_DEVICE

# application/ is a symlink to the shared tree in MDUV380_firmware (-H follows it)
C_SRCS := $(shell find -H $(SRC_ROOTS) -name '*.c')
# Exclusion from .cproject
C_SRCS := $(filter-out application/include/user_interface/languages/src/languages_builder.c,$(C_SRCS))
# RT3S / MDUV380 only sources
C_SRCS := $(filter-out application/source/hardware/AT1846S.c application/source/hardware/HX8353E_display.c application/source/user_interface/menuThemeOptions.c application/source/user_interface/uiEmcom.c,$(C_SRCS))

S_SRCS := $(shell find -H $(SRC_ROOTS) -name '*.s' -o -name '*.S')

OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o) $(S_SRCS:.s=.o))
OBJS := $(OBJS:.S=.o)
DEPS := $(OBJS:.o=.d)

# --- targets -----------------------------------------------------------------
.PHONY: all clean prepare flash-bin

all: $(BUILD_DIR)/$(OUT_NAME).bin

# Step 1: generate the codec placeholder before compilation
prepare:
	@cd application/source/linkerdata && python3 ../../../tools/codec_cleaner.py -C

# Step 2: compile/link
$(BUILD_DIR)/$(TARGET).elf: prepare $(OBJS) $(LDSCRIPT)
	@echo "  LD    $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@
	@$(SIZE) $@

# Step 3: ELF -> raw .bin
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo "  BIN   $@"
	@$(OBJCOPY) -O binary -S $< $@

# Step 4: codec_cleaner post-process -> $(OUT_NAME).bin
$(BUILD_DIR)/$(OUT_NAME).bin: $(BUILD_DIR)/$(TARGET).bin
	@echo "  CLEAN $@"
	@cd $(BUILD_DIR) && python3 ../tools/codec_cleaner.py -i $(TARGET).bin -o $(OUT_NAME).bin
	@ls -la $@

# pattern rules
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

# Like clean, but keeps dist/ (used between build-all iterations).
clean-build:
	rm -rf $(BUILD_DIR)

# --- containerised build (run from the HOST) --------------------------------
# The build needs a Linux ARM toolchain, so "make build" drives a podman
# container defined by Containerfile in this directory. Override the engine
# (e.g. docker) or image tag from the command line:
#     make build CONTAINER_ENGINE=docker
#
# Inside the container the in-container "make all" runs the same rules as a
# native build, so once the image is cached "make build" is a thin wrapper.
CONTAINER_ENGINE ?= podman
IMG_TAG          ?= opengd77-mduv380-builder:latest
JOBS             ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
SRC_DIR          := $(abspath $(CURDIR)/..)

CONTAINER_RUN = $(CONTAINER_ENGINE) run --rm \
                -v $(SRC_DIR):/src:Z \
                -w /src/MD9600_firmware \
                -e HW=$(HW) \
                $(IMG_TAG)

.PHONY: image build rebuild help

image:
	@$(CONTAINER_ENGINE) image inspect $(IMG_TAG) >/dev/null 2>&1 || \
	    $(CONTAINER_ENGINE) build -f $(CURDIR)/Containerfile -t $(IMG_TAG) $(CURDIR)

build: image
	$(CONTAINER_RUN) make -j$(JOBS) all HW=$(HW)
	@test -f $(BUILD_DIR)/$(OUT_NAME).bin && ls -la $(BUILD_DIR)/$(OUT_NAME).bin
	@command -v shasum >/dev/null 2>&1 && shasum -a 256 $(BUILD_DIR)/$(OUT_NAME).bin || true

rebuild: image
	$(CONTAINER_RUN) sh -c 'make clean && make -j$(JOBS) all HW=$(HW)'

help:
	@echo 'OpenGD77 MD-9600 firmware -- make targets (run from MD9600_firmware/):'
	@echo '  make build     Compile build/$(OUT_NAME).bin in the container (HW=5 default)'
	@echo '  make rebuild   Clean and compile from scratch'
	@echo '  make flash     Flash it to an MD-9600 held in DFU mode'
	@echo '  make clean     Remove build/'

# --- flash (run on the HOST; radio in DFU mode) --------------------------------
LOADER       ?= tools/opengd77_stm32_firmware_loader.py
PYTHON       ?= python3
FIRMWARE_BIN := $(BUILD_DIR)/$(OUT_NAME).bin

.PHONY: flash
flash:
	@test -f $(FIRMWARE_BIN) || { echo "ERROR: $(FIRMWARE_BIN) missing - run 'make build' first." >&2; exit 1; }
	$(PYTHON) $(LOADER) -m MD-9600 -f $(FIRMWARE_BIN)

-include $(DEPS)
