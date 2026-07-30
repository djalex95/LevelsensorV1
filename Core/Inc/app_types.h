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

/* Sensorkonstanten der gebauten Hardwarevariante. Der Include-Pfad
 * Core/Inc/variants/<HW_VARIANT> kommt aus dem Makefile; in der CubeIDE
 * muss er einmalig unter Project > Properties > C/C++ Build > Settings >
 * MCU GCC Compiler > Include paths eingetragen werden. */
#include "sensor_cfg.h"

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

/* Werkswerte fuer die Druckmessung. max_val in Einheiten von 0,1 mBar,
 * also 1000 = 100,0 mBar = 100000 uBar. Der Wert haengt vom Messbereich
 * des bestueckten Sensors ab und kommt deshalb aus der Variante. */
#define std_press SENSOR_STD_PRESS
#define std_offset 0

/* Fehler-Flags in error_mode (Bitmaske; steuern die Fehler-LED und das
 * E=-Feld im BLE-STAT). ERROR_I2C war historisch 10 (Bits 1 und 3) -
 * jetzt echte Einzelbits. */
#define ERROR_TX_CAN 1
#define ERROR_I2C    2
#define ERROR_HWV    4	/* OTP-Variante widerspricht der Firmware (Issue #2) */

#endif /* INC_APP_TYPES_H_ */
