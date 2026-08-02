/*
 * sensor.h
 *
 * Sensor-/Mess-Ebene: Drucksensor (I2C), Prozentrechnung, Tankform-
 * Linearisierung und Analogausgang (DAC). Aus main.c herausgezogen;
 * Verhalten und Signaturen unveraendert.
 *
 * Die Funktionen nutzen die in main.c definierten CubeMX-Handles
 * (hi2c1, hdac1) sowie den App-Zustand (EEPROM_values, device_param,
 * error_mode) per extern - siehe sensor_common.c und den jeweiligen
 * Treiber sensor_legacy.c / sensor_pdms.c.
 */
#ifndef INC_SENSOR_H_
#define INC_SENSOR_H_

#include <stdint.h>
#include "app_types.h"

/* Drucksensor (I2C) initialisieren: erste Messung anstossen und warten,
 * bis das Busy-Bit geloescht ist (mit Versuchslimit). */
void init_Sensor(void);

/* Eine Messung lesen (Druck in uBar-Einheiten, Temperatur in 0,01 Grad C).
 * Bei I2C-Fehler wird ERROR_I2C in error_mode gesetzt und der letzte
 * gueltige Messwert zurueckgegeben. */
sensor_mess get_value(void);

/* Zwischenwerte der letzten Messung, nur fuer die Diagnose ueber
 * NMEA2000 (Kommando 0x07). Der jeweilige Treiber legt sie in
 * get_value() ab, definiert sind sie im variantenneutralen Teil - so
 * kommt nmea_app.c ohne Abfrage von SENSOR_KIND aus.
 *
 *   sensor_raw_p    Rohwert des Drucks, wie aus dem Sensor gelesen
 *                   (PDMS: P16 als 0..65535; alt: raw24 vorzeichenbehaftet)
 *   sensor_raw_t    Rohwert der Temperatur
 *   sensor_delta    Abstand zur Bereichsmitte, vorzeichenbehaftet und
 *                   UNBEGRENZT (beim alten Sensor gleich sensor_raw_p)
 *   sensor_ubar_raw Druck in uBar VOR Abzug des Offsets
 *   sensor_sat      1 = Rohwert lag ausserhalb des Nennbereichs
 */
extern volatile int32_t sensor_raw_p;
extern volatile int32_t sensor_raw_t;
extern volatile int32_t sensor_delta;
extern volatile int32_t sensor_ubar_raw;
extern volatile uint8_t sensor_sat;

/* Fuellhoehe aus Druck: 0..10000 = 0..100,00 %, begrenzt auf die
 * Kalibrierung (max_val). max_val == 0 -> 0 (Div-durch-0-Schutz). */
uint16_t calc_percent(calib_data *datas, int64_t mw);

/* Fuellhoehe (0..10000) ueber die 11 Stuetzstellen der Tankform aufs
 * Volumen abbilden (stueckweise lineare Interpolation). */
uint16_t linearize_percent(uint16_t raw);

/* Analogausgang: Fuellstand in Prozent (0..10000) als 0,5..4,5 V. */
void set_volt(uint16_t percent, dac_calib_data *datas);

/* Analogausgang: rohen DAC-Wert setzen (Kalibrierung/Abgleich). */
void set_volt_raw(uint16_t volt, dac_calib_data *datas);

#endif /* INC_SENSOR_H_ */
