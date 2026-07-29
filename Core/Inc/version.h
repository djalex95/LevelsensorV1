/*
 * version.h
 *
 * Zentrale Firmware-Version. Wird von der NMEA2000-Produktinfo (PGN 126996)
 * und der BLE-Statusausgabe genutzt, damit es nur eine Quelle gibt.
 */
#ifndef INC_VERSION_H_
#define INC_VERSION_H_

/* Firmware-Version der Applikation. Bei jeder Freigabe erhöhen. */
#define FW_VERSION       "1.2.10-dev"
#define FW_VERSION_DATE  "2026-07-28"

/* Kombinierte Anzeige, z. B. "1.2.0 (2026-07-07)". */
#define FW_VERSION_STR   FW_VERSION " (" FW_VERSION_DATE ")"

/* Hardware-Revision (4-stellig, selten geändert). */
#define HW_REV           1000
#define HW_REV_STR       "1000"

/* Hardware-Variante (Messprinzip/Frontend), unabhaengig von der Revision:
 *   1000 = Drucksensor V1        alter 24-Bit-Sensor, Platine V1
 *   1001 = Drucksensor V2        WSEN-PDMS +/-10 kPa, Platine V2
 *   1002 = Ultraschall           (geplant)
 *   1003 = Drucksensor V2 flach  WSEN-PDMS +/-1 kPa, Platine V2
 *
 * 1001 und 1003 nutzen dieselbe Platine und unterscheiden sich nur im
 * bestueckten Sensor. Die Firmware kann das nicht selbst erkennen - der
 * Sensor meldet seinen Messbereich nicht ueber I2C. Eine vertauschte
 * Firmware faellt deshalb nicht durch einen Fehler auf, sondern nur durch
 * einen um Faktor zehn falschen Fuellstand.
 *
 * Wird im BLE-STAT als HWV gemeldet, damit App/Bootloader die passende
 * Firmware zuordnen koennen (Schutz vor Cross-Flashing, siehe
 * ARCHITECTURE.md). Aktuell Compile-Konstante je Build (das Makefile
 * setzt sie per -DHW_VARIANT); spaeter aus dem OTP verankert und beim
 * Boot dagegen geprueft. */
#ifndef HW_VARIANT
#define HW_VARIANT       1000
#endif

#endif /* INC_VERSION_H_ */
