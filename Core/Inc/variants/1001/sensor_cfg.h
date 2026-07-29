/*
 * sensor_cfg.h - Variante 1001: Drucksensor V2
 *
 * Platine V2 (STM32G0B1KBU6, ohne N), Wuerth WSEN-PDMS 2513130810205
 * mit +/-10 kPa. Nur Zahlen, keine HAL-Abhaengigkeit - dadurch auf dem
 * PC testbar (siehe tests/test_sensor_conv.c).
 */
#ifndef INC_VARIANT_SENSOR_CFG_H_
#define INC_VARIANT_SENSOR_CFG_H_

#include "sensor_kind.h"
#include "version.h"

/* Abgleich Include-Pfad <-> HW_VARIANT: Welche dieser Dateien gilt,
 * entscheidet der Include-Pfad; was die Firmware als HWV meldet, das
 * Symbol HW_VARIANT. Das Makefile setzt beides gemeinsam, die CubeIDE
 * dagegen in zwei getrennten Feldern (Include paths / Define symbols).
 * Ohne diese Abfrage entstuende bei einem Fehlgriff eine Firmware, die
 * richtig misst, sich aber falsch ausgibt - genau die Verwechslung,
 * gegen die die HWV schuetzen soll. */
#if HW_VARIANT != 1001
#error "Variante 1001 eingebunden, aber HW_VARIANT steht anders (CubeIDE: Include paths und Define symbols abgleichen)."
#endif

#define SENSOR_KIND         SENSOR_KIND_PDMS
#define SENSOR_NAME         "WSEN-PDMS +/-10 kPa"

/* I2C-Adresse (7 Bit): SA0/CS liegt auf der Platine an GND -> 0x6C.
 * (An VDD waere es 0x6E; die CRC-geschuetzten Adressen nutzen wir nicht.) */
#define SENSOR_I2C_ADDR7    0x6C

/* Vollausschlag je Richtung in uBar (0,1 Pa): 10 kPa = 100 mBar. */
#define SENSOR_FS_UBAR      100000L

/* Werksvorgabe fuer max_val (Druck bei 100 %) in Einheiten von 0,1 mBar.
 * 1000 = 100,0 mBar = Vollausschlag = rund 1,02 m Wassersaeule. */
#define SENSOR_STD_PRESS    1000

#endif /* INC_VARIANT_SENSOR_CFG_H_ */
