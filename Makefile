# Eigenständiges Makefile für die Firmware (unabhängig von STM32CubeIDE).
# Baut die App-Partition (Linkerscript STM32G0B1KBUXN_FLASH.ld, ab 0x08008000).
# Ergebnis: build/CAN_FuellstandsensorBLE.elf / .bin / .hex
#
# Verwendung:
#   make            # bauen
#   make clean      # aufräumen
# Voraussetzung: arm-none-eabi-gcc im PATH.

TARGET    = CAN_FuellstandsensorBLE
BUILD_DIR = build

# Optimierung: -O0 entspricht dem getesteten CubeIDE-Debug-Build.
OPT ?= -O0
DEBUG ?= 1

######################################
# Quellen
######################################
C_SOURCES = \
  $(wildcard Core/Src/*.c) \
  $(wildcard Drivers/STM32G0xx_HAL_Driver/Src/*.c)

ASM_SOURCES = Core/Startup/startup_stm32g0b1kbuxn.s

LDSCRIPT = STM32G0B1KBUXN_FLASH.ld

######################################
# Toolchain
######################################
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
CP      = $(PREFIX)objcopy
SZ      = $(PREFIX)size

######################################
# CPU / Defines / Includes
######################################
CPU       = -mcpu=cortex-m0plus
FLOAT-ABI = -mfloat-abi=soft
MCU       = $(CPU) -mthumb $(FLOAT-ABI)

C_DEFS = -DUSE_HAL_DRIVER -DSTM32G0B1xx
ifeq ($(DEBUG),1)
C_DEFS += -DDEBUG
endif

C_INCLUDES = \
  -ICore/Inc \
  -IDrivers/STM32G0xx_HAL_Driver/Inc \
  -IDrivers/STM32G0xx_HAL_Driver/Inc/Legacy \
  -IDrivers/CMSIS/Device/ST/STM32G0xx/Include \
  -IDrivers/CMSIS/Include

######################################
# Flags
######################################
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -std=gnu11 -Wall \
          -fdata-sections -ffunction-sections -g -gdwarf-2 -MMD -MP

LIBS    = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

######################################
# Objektdateien
######################################
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

######################################
# Regeln
######################################
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(CP) -O binary $< $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(CP) -O ihex $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
