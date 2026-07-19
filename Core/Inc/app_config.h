/*
 * app_config.h
 *
 * Konfigurations-/EEPROM-Helfer: lesen und schreiben der einzelnen
 * Datensaetze im robusten Config-Speicher (config_store.c, Bytes des
 * 64-Byte-Blocks cfg_data). Aus main.c herausgezogen, damit die
 * Config-Zugriffe an einer Stelle liegen.
 *
 * Alle Funktionen arbeiten auf dem RAM-Cache cfg_data[] und rufen bei
 * Aenderungen config_save() (atomares Ping-Pong-Schreiben in den Flash).
 */
#ifndef INC_APP_CONFIG_H_
#define INC_APP_CONFIG_H_

#include <stdint.h>
#include "app_types.h"
#include "nmea2000.h"

/* Kalibrierung (Drucksensor) */
uint8_t check_EEPROM(void);
void get_EEPROM(calib_data *values);
void save_EEPROM(calib_data *values);

/* Kalibrierung (DAC-Ausgang) */
uint8_t check_dac_EEPROM(void);
void get_dac_EEPROM(dac_calib_data *values);

/* Produkt-/Tankparameter (Fluidtyp, Kapazitaet, Linearisierung) */
void set_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values);
void get_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values);

/* NMEA2000-Quelladresse (Config-Byte 30) */
uint8_t get_adr_eeprom(void);
void set_adr_eeprom(uint8_t adr);

/* Sensorname (Config-Bytes 33..56) */
void get_name_eeprom(char *buf);
void set_name_eeprom(const char *name);

/* Stuetzstellen-Tabelle pruefen (0..100, monoton nicht fallend). */
uint8_t lin_table_valid(uint8_t *t);

#endif /* INC_APP_CONFIG_H_ */
