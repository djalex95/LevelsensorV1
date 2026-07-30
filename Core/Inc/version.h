/*
 * version.h
 *
 * Zentrale Firmware-Version. Wird von der NMEA2000-Produktinfo (PGN 126996)
 * und der BLE-Statusausgabe genutzt, damit es nur eine Quelle gibt.
 */
#ifndef INC_VERSION_H_
#define INC_VERSION_H_

/* Firmware-Version der Applikation. Bei jeder Freigabe erhöhen. */
#define FW_VERSION       "1.2.11"
#define FW_VERSION_DATE  "2026-07-30"

/* Kombinierte Anzeige, z. B. "1.2.0 (2026-07-07)". */
#define FW_VERSION_STR   FW_VERSION " (" FW_VERSION_DATE ")"

/* Hardware-Variante (Messprinzip/Frontend):
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
 * Nach aussen werden Variante und Platinen-Stand als EINE Kennung
 * gemeldet, z. B. "1003A" (BLE-STAT als HWV, PGN 126996 als
 * ModelVersion): die Zahl entscheidet ueber die passende Firmware
 * (Schutz vor Cross-Flashing, siehe ARCHITECTURE.md), der Buchstabe
 * ist der Hardware-Stand der Platine - rein informativ, gleiche
 * Firmware. Braucht eine Platinenaenderung andere Firmware, gibt es
 * eine NEUE Nummer, keinen neuen Buchstaben. Der Buchstabe kommt als
 * HW_REV_SUFFIX aus variants/<n>/sensor_cfg.h; die fruehere separate
 * Nummer HW_REV (stand auch auf der V2-Platine immer auf 1000) ist
 * ersatzlos entfallen, ebenso das Feld HW= im BLE-STAT.
 *
 * Aktuell Compile-Konstante je Build (das Makefile
 * setzt sie per -DHW_VARIANT); spaeter aus dem OTP verankert und beim
 * Boot dagegen geprueft. */
#ifndef HW_VARIANT
#define HW_VARIANT       1000
#endif

/* Variante als String plus Platinen-Buchstabe, z. B. "1003A".
 * HW_REV_SUFFIX kommt aus der sensor_cfg.h der Variante - fehlt sie im
 * Include-Pfad, bricht der Build an der Verwendungsstelle ab. */
#define HWV_STR_(x)      #x
#define HWV_STR(x)       HWV_STR_(x)
#define HWV_FULL_STR     HWV_STR(HW_VARIANT) HW_REV_SUFFIX

#endif /* INC_VERSION_H_ */
