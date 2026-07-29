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

#define SENSOR_KIND         SENSOR_KIND_PDMS
#define SENSOR_NAME         "WSEN-PDMS +/-1 kPa (flach)"

/* Gleiche Platine, gleiche Beschaltung -> gleiche Adresse wie 1001. */
#define SENSOR_I2C_ADDR7    0x6C

/* Vollausschlag je Richtung in uBar (0,1 Pa): 1 kPa = 10 mBar. */
#define SENSOR_FS_UBAR      10000L

/* Werksvorgabe fuer max_val (Druck bei 100 %) in Einheiten von 0,1 mBar.
 * 100 = 10,0 mBar = Vollausschlag = rund 10,2 cm Wassersaeule. */
#define SENSOR_STD_PRESS    100

#endif /* INC_VARIANT_SENSOR_CFG_H_ */
