/*
 * ble_app.h
 *
 * BLE-Kommando-Ebene der Applikation: Status-/Kennlinien-Ausgabe an die App
 * und Verarbeitung der Textkommandos (Spezifikation in
 * PC_Tools/BLE_Protokoll.md). Aus main.c herausgezogen; Verhalten und
 * Signaturen unveraendert.
 *
 * Der eigentliche Proteus-e-Treiber (UART-Frames, Reset, Namen setzen)
 * liegt weiterhin in ble.c. Die Funktionen hier nutzen den App-Zustand
 * aus main.c (percent_val, sensor_data_rx, dev_info_par, ...) per extern -
 * siehe ble_app.c.
 */
#ifndef INC_BLE_APP_H_
#define INC_BLE_APP_H_

#include <stdint.h>

/* Aktuellen Sensorzustand als STAT;...-Zeile an die App senden. */
void ble_send_status(void);

/* Ein Textkommando von der App verarbeiten (VER, GET, LIN, CAL100,
 * CALRESET, FLUID, CAP, INST, NAME, FACTORYRESET, DFU). */
void ble_handle_command(const uint8_t *data, uint16_t len);

/* Gewuenschter BLE-Modulname: gespeicherter Sensorname, sonst Default
 * "LevelSense-<UID>". buf braucht mind. 21 Bytes. */
void ble_desired_name(char *buf);

#endif /* INC_BLE_APP_H_ */
