/*
 * sensor_cfg.h - Variante 1000: Drucksensor V1
 *
 * Platine V1 (STM32G0B1KBU6N), alter 24-Bit-Drucksensor am I2C-Bus.
 * Nur Zahlen, keine HAL-Abhaengigkeit - dadurch auf dem PC testbar
 * (siehe tests/test_sensor_conv.c).
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
#if HW_VARIANT != 1000
#error "Variante 1000 eingebunden, aber HW_VARIANT steht anders (CubeIDE: Include paths und Define symbols abgleichen)."
#endif

#define SENSOR_KIND         SENSOR_KIND_LEGACY
#define SENSOR_NAME         "Drucksensor V1 (24 Bit)"

/* I2C-Adresse (7 Bit, ohne R/W-Bit). */
#define SENSOR_I2C_ADDR7    0x6D

/* Nenn-Messbereich in uBar (0,1 Pa) - nur zur Dokumentation und fuer
 * Plausibilitaetspruefungen, geht nicht in die Umrechnung ein. */
#define SENSOR_FS_UBAR      400000L      /* 40 kPa = 400 mBar */

/* Werksvorgabe fuer max_val (Druck bei 100 %) in Einheiten von 0,1 mBar.
 * 1000 = 100,0 mBar = 100000 uBar, also rund ein Viertel des Messbereichs
 * bzw. etwa 1,02 m Wassersaeule. */
#define SENSOR_STD_PRESS    1000

#endif /* INC_VARIANT_SENSOR_CFG_H_ */
