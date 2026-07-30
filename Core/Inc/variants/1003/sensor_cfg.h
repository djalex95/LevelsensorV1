/*
 * sensor_cfg.h - Variante 1003: Drucksensor V2 flach
 *
 * Dieselbe Platine V2 wie Variante 1001, aber mit dem Wuerth WSEN-PDMS
 * 2513130810105 (+/-1 kPa) bestueckt. Gedacht fuer flache Tanks - der
 * Messbereich entspricht rund 10,2 cm Wassersaeule (Diesel ca. 12,3 cm).
 *
 * Achtung: Der Unterschied zu 1001 ist NICHT elektrisch, sondern nur das
 * bestueckte Bauteil. Die Firmware kann den Messbereich nicht selbst
 * erkennen - der Sensor meldet seine Bestellnummer nicht ueber I2C.
 * Einziger Schutz gegen eine vertauschte Firmware ist die HWV-Meldung
 * ueber BLE. Ein um Faktor zehn falscher Fuellstand faellt sonst nicht
 * auf, er sieht einfach plausibel aus.
 */
#ifndef INC_VARIANT_SENSOR_CFG_H_
#define INC_VARIANT_SENSOR_CFG_H_

#include "sensor_kind.h"
#include "version.h"

/* Abgleich Include-Pfad <-> HW_VARIANT: Welche dieser Dateien gilt,
 * entscheidet der Include-Pfad; was die Firmware als HWV meldet, das
 * Symbol HW_VARIANT. Das Makefile setzt beides gemeinsam, die CubeIDE
 * dagegen in zwei getrennten Feldern (Include paths / Define symbols).
 * Bei 1001 gegen 1003 ist das besonders heikel: beide bauen denselben
 * Treiber, der Unterschied steckt nur im Messbereich. Ohne diese
 * Abfrage entstuende eine Firmware, die richtig misst, sich aber falsch
 * ausgibt - genau die Verwechslung, gegen die die HWV schuetzen soll. */
#if HW_VARIANT != 1003
#error "Variante 1003 eingebunden, aber HW_VARIANT steht anders (CubeIDE: Include paths und Define symbols abgleichen)."
#endif

#define SENSOR_KIND         SENSOR_KIND_PDMS
#define SENSOR_NAME         "WSEN-PDMS +/-1 kPa (flach)"

/* Gleiche Platine, gleiche Beschaltung -> gleiche Adresse wie 1001. */
#define SENSOR_I2C_ADDR7    0x6C

/* Vollausschlag je Richtung in uBar (0,1 Pa): 1 kPa = 10 mBar. */
#define SENSOR_FS_UBAR      10000L

/* Werksvorgabe fuer max_val (Druck bei 100 %) in Einheiten von 0,1 mBar.
 * 100 = 10,0 mBar = Vollausschlag = rund 10,2 cm Wassersaeule. */
#define SENSOR_STD_PRESS    100

/* Platinen-Stand der V2-Platine, Teil der HWV-Meldung ("1003A").
 * ACHTUNG: 1001 und 1003 sitzen auf DERSELBEN Platine. Bei einer
 * Hardware-Aenderung ohne Firmware-Relevanz (z. B. Widerstandstausch)
 * den Buchstaben in BEIDEN sensor_cfg.h gleichzeitig hochzaehlen. */
#define HW_REV_SUFFIX       "A"

#endif /* INC_VARIANT_SENSOR_CFG_H_ */
