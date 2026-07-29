# Eigenständiges Makefile für die Firmware (unabhängig von STM32CubeIDE).
# Baut die App-Partition (Linkerscript STM32G0B1KBUXN_FLASH.ld, ab 0x08008000).
# Ergebnis: build/<HW_VARIANT>/CAN_FuellstandsensorBLE.elf / .bin / .hex
#
# Verwendung:
#   make                      # Standardvariante 1001 bauen
#   make HW_VARIANT=1000      # andere Hardwarevariante bauen
#   make clean                # aufräumen (alle Varianten)
# Voraussetzung: arm-none-eabi-gcc im PATH.

TARGET    = CAN_FuellstandsensorBLE

######################################
# Hardwarevariante
######################################
# 1000 = Drucksensor V1 (alter 24-Bit-Sensor, STM32G0B1KBU6N)
# 1001 = Drucksensor V2 (Würth WSEN-PDMS ±10 kPa)
# 1003 = Drucksensor V2 flach (Würth WSEN-PDMS ±1 kPa, gleiche Platine)
#
# Die Variante bestimmt die Sensorkonstanten (Core/Inc/variants/<id>/) und
# welcher Sensortreiber übersetzt wird. Sie geht außerdem als HW_VARIANT in
# die Firmware ein und wird über BLE als HWV gemeldet.
HW_VARIANT ?= 1001

ifeq ($(wildcard Core/Inc/variants/$(HW_VARIANT)/sensor_cfg.h),)
  $(error Unbekannte HW_VARIANT '$(HW_VARIANT)' - vorhanden: $(notdir $(wildcard Core/Inc/variants/*)))
endif

# Getrennte Ausgabeverzeichnisse, damit ein Variantenwechsel nicht gegen
# alte Objektdateien linkt.
BUILD_DIR = build/$(HW_VARIANT)

# Optimierung: -O0 entspricht dem bisher auf Hardware getesteten Stand
# (wie der CubeIDE-Debug-Build). -Og/-Os erst nach einem Hardware-Test
# als Standard setzen; bis dahin manuell: make OPT=-Og
OPT ?= -O0
DEBUG ?= 1

######################################
# Quellen
######################################
# Genau ein Sensortreiber wird eingebunden; die übrigen werden aus dem
# Wildcard herausgefiltert, damit sie nicht doppelt definierte Symbole
# liefern.
SENSOR_ALL = Core/Src/sensor_legacy.c Core/Src/sensor_pdms.c

ifeq ($(HW_VARIANT),1000)
  SENSOR_SRC = Core/Src/sensor_legacy.c
else
  SENSOR_SRC = Core/Src/sensor_pdms.c
endif

C_SOURCES = \
  $(filter-out $(SENSOR_ALL),$(wildcard Core/Src/*.c)) \
  $(SENSOR_SRC) \
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

C_DEFS = -DUSE_HAL_DRIVER -DSTM32G0B1xx -DHW_VARIANT=$(HW_VARIANT)
ifeq ($(DEBUG),1)
C_DEFS += -DDEBUG
endif

C_INCLUDES = \
  -ICore/Inc \
  -ICore/Inc/variants/$(HW_VARIANT) \
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

# Alle Varianten entfernen, nicht nur die gerade gewählte.
clean:
	rm -rf build

# Host-Tests (laufen auf dem PC, kein Target nötig).
test:
	sh tests/run_tests.sh

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean test
