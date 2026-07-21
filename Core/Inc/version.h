/*
 * version.h
 *
 * Zentrale Firmware-Version. Wird von der NMEA2000-Produktinfo (PGN 126996)
 * und der BLE-Statusausgabe genutzt, damit es nur eine Quelle gibt.
 */
#ifndef INC_VERSION_H_
#define INC_VERSION_H_

/* Firmware-Version der Applikation. Bei jeder Freigabe erhöhen. */
#define FW_VERSION       "1.2.9-dev"
#define FW_VERSION_DATE  "2026-07-20"

/* Kombinierte Anzeige, z. B. "1.2.0 (2026-07-07)". */
#define FW_VERSION_STR   FW_VERSION " (" FW_VERSION_DATE ")"

/* Hardware-Revision (4-stellig, selten geändert). */
#define HW_REV           1000
#define HW_REV_STR       "1000"

/* Hardware-Variante (Messprinzip/Frontend), unabhaengig von der Revision:
 *   1000 = Drucksensor V1   (aktuell)
 *   1001 = Drucksensor V2   (geplant)
 *   1002 = Ultraschall      (geplant)
 * Wird im BLE-STAT als HWV gemeldet, damit App/Bootloader die passende
 * Firmware zuordnen koennen (Schutz vor Cross-Flashing, siehe
 * ARCHITECTURE.md). Aktuell Compile-Konstante je Build; spaeter aus dem
 * OTP verankert und beim Boot dagegen geprueft. */
#ifndef HW_VARIANT
#define HW_VARIANT       1000
#endif

#endif /* INC_VERSION_H_ */
