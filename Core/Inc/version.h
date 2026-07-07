/*
 * version.h
 *
 * Zentrale Firmware-Version. Wird von der NMEA2000-Produktinfo (PGN 126996)
 * und der BLE-Statusausgabe genutzt, damit es nur eine Quelle gibt.
 */
#ifndef INC_VERSION_H_
#define INC_VERSION_H_

/* Firmware-Version der Applikation. Bei jeder Freigabe erhöhen. */
#define FW_VERSION       "1.2.0"
#define FW_VERSION_DATE  "2026-07-07"

/* Kombinierte Anzeige, z. B. "1.2.0 (2026-07-07)". */
#define FW_VERSION_STR   FW_VERSION " (" FW_VERSION_DATE ")"

/* Hardware-/Modellstand (selten geändert). */
#define HW_VERSION_STR   "1.1.0"

#endif /* INC_VERSION_H_ */
