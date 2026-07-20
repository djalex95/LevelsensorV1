/*
 * nmea_app.h
 *
 * NMEA2000-Handler der Applikationsebene: verarbeiten die von nmea2000.c
 * reassemblierten Fast-Packet-Nachrichten (gf_buf/gf_len/gf_src/gf_pgn).
 * Aus main.c herausgezogen; Verhalten und Signaturen unveraendert.
 *
 * Die Handler nutzen den App-Zustand aus main.c (dev_info_par,
 * device_param, EEPROM_values, sensor_name, ...) per extern - siehe
 * nmea_app.c. Aufruf aus der Hauptschleife, sobald gf_ready gesetzt ist.
 */
#ifndef INC_NMEA_APP_H_
#define INC_NMEA_APP_H_

/* Group Function (PGN 126208): Request/Command auf 127505 (Fluid Level:
 * Instanz, Fluidtyp, Kapazitaet) und 126998 (Sensorname), mit Acknowledge. */
void handle_group_function(void);

/* Proprietaere Konfiguration (PGN 126720): Stuetzstellen lesen/schreiben,
 * Kalibrierung setzen/zuruecksetzen, Werksreset. */
void handle_prop_config(void);

/* PGN 65240 Commanded Address: neue Quelladresse, wenn der 64-bit-NAME
 * exakt unserer ist. */
void handle_commanded_address(void);

#endif /* INC_NMEA_APP_H_ */
