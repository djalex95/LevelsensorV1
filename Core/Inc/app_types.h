/*
 * app_types.h
 *
 * Gemeinsame Datentypen und Grundkonstanten der Applikation. Frueher lagen
 * diese Typen direkt in main.c; sie wurden herausgezogen, damit die
 * ausgelagerten Module (app_config, spaeter sensor/nmea/ble_app) dieselben
 * Strukturen nutzen koennen, ohne von main.c abzuhaengen.
 */
#ifndef INC_APP_TYPES_H_
#define INC_APP_TYPES_H_

#include <stdint.h>

/* Kalibrierdaten des Drucksensors (Config-Bytes 0..4). */
typedef struct calib_data{
	uint8_t calib_available;
	uint32_t max_val;
	int16_t offset;
}calib_data;

/* Kalibrierung des Analogausgangs (DAC), Config-Bytes 8..16. */
typedef struct dac_calib_data{
	uint8_t calib_available;	// soll auch genutzt werden, um min und max daten am Ausgang zu sehen !
	int32_t dac_mx;
	int32_t dac_c;
}dac_calib_data;

/* Produkt-/Tankparameter (Fluidtyp, Kapazitaet, Linearisierung). */
typedef struct prod_param{
	uint8_t fluid_type;
	uint8_t tank_cap;
	uint8_t lin_point[11];
}prod_param;

/* Ein Messwert des Sensors: Druck (uBar) und Temperatur. */
typedef struct sensor_data{
	int32_t pressure;
	int16_t temp;
}sensor_mess;

/* Werkswerte fuer die Druckmessung. */
#define std_press 1000	//100,0mBar -> 100000uBar
#define std_offset 0

/* Fehler-Flags in error_mode (Bitmaske; steuern die Fehler-LED). */
#define ERROR_TX_CAN 1
#define ERROR_I2C 10

#endif /* INC_APP_TYPES_H_ */
