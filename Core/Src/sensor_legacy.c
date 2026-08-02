/*
 * sensor_legacy.c
 *
 * Einlesen des alten 24-Bit-Drucksensors (I2C 0x6D, Messung per
 * Kommando angestossen). Inhaltlich unveraendert aus sensor.c
 * uebernommen; die Adresse kommt jetzt aus der Variante
 * (sensor_cfg.h), die Umrechnung aus sensor_scale.h.
 *
 * Wird nur bei HW_VARIANT=1000 gebaut: das Makefile bindet nur den
 * passenden Treiber ein, und zusaetzlich ist der gesamte Inhalt in eine
 * Variantenabfrage gefasst. Letzteres ist fuer die CubeIDE noetig, die
 * ohne Weiteres jede Datei in Core/Src uebersetzt - bei der falschen
 * Variante bleibt hier einfach eine leere Uebersetzungseinheit uebrig.
 */
#include "main.h"
#include "sensor.h"
#include "sensor_scale.h"

#if SENSOR_KIND == SENSOR_KIND_LEGACY

/* CubeMX-Handles und App-Zustand - definiert in main.c. */
extern I2C_HandleTypeDef hi2c1;
extern volatile uint8_t error_mode;
extern calib_data EEPROM_values;

#define SENSOR_ADDR8    (SENSOR_I2C_ADDR7 << 1)

void init_Sensor(void)
{
	uint8_t tx_arr[2];
	tx_arr[0] = 0x30;
	tx_arr[1] = 0x0A;
	/* Fix: vorher 'while(cmd_reg % 0x08)' mit cmd_reg=0 -> Schleife lief nie.
	 * Jetzt: warten bis Busy-Bit (0x08 in Reg 0x30) geloescht ist,
	 * mit Versuchslimit statt Endlosschleife bei fehlendem Sensor. */
	uint8_t cmd_reg = 0x08;
	uint8_t retries = 0;

	HAL_I2C_Master_Transmit(&hi2c1, SENSOR_ADDR8, tx_arr, 2, 25);

	while ((cmd_reg & 0x08) && (retries++ < 100))
	{
		HAL_I2C_Master_Transmit(&hi2c1, SENSOR_ADDR8, tx_arr, 1, 25);

		HAL_I2C_Master_Receive(&hi2c1, SENSOR_ADDR8, &cmd_reg, 1, 25);
	}

}

sensor_mess get_value(void)
{
	static sensor_mess last_good = {0, 0};	/* letzter gueltiger Messwert */
	sensor_mess mess_data;
	uint8_t rxBuffer[5] = {0};
	uint8_t start_Reg = 0x06;
	uint8_t tx_arr[2];
	tx_arr[0] = 0x30;
	tx_arr[1] = 0x0A;
	int32_t raw24, t16;
	uint8_t i2c_ok = 1;

	/* Fix: Timeout 25 ms statt 2500 ms - blockiert die Hauptschleife
	 * bei Sensorausfall nicht mehr sekundenlang. */
	if(HAL_I2C_Master_Transmit(&hi2c1, SENSOR_ADDR8, &start_Reg, 1, 25)!= HAL_OK)
	{
		error_mode |= ERROR_I2C;
		i2c_ok = 0;
	}

	if(i2c_ok && (HAL_I2C_Master_Receive(&hi2c1, SENSOR_ADDR8, rxBuffer, 5, 25) != HAL_OK))
	{
		error_mode |= ERROR_I2C;
		i2c_ok = 0;
	}

	/* naechste Messung anstossen (auch nach Fehler versuchen) */
	HAL_I2C_Master_Transmit(&hi2c1, SENSOR_ADDR8, tx_arr, 2, 25);

	if(i2c_ok == 0)
	{
		/* Fix: bei I2C-Fehler keinen uninitialisierten Puffer auswerten,
		 * sondern letzten gueltigen Wert zurueckgeben. */
		return last_good;
	}

	raw24 = ((int32_t)rxBuffer[0] << 16) | ((int32_t)rxBuffer[1] << 8) | rxBuffer[2];
	if (rxBuffer[0] & 0x80)
	{
		raw24 -= 16777216;	/* 24-bit-Zweierkomplement */
	}
	t16 = ((int32_t)rxBuffer[3] << 8) | rxBuffer[4];
	if (rxBuffer[3] & 0x80)
	{
		t16 -= 65536;		/* 16-bit-Zweierkomplement */
	}

	/* Zwischenwerte fuer die Diagnose mitschreiben (Kommando 0x07).
	 * Der alte Sensor liefert den Wert schon vorzeichenbehaftet, es gibt
	 * hier also keinen Abstand zu einer Bereichsmitte: sensor_delta ist
	 * derselbe Rohwert. */
	sensor_raw_p = raw24;
	sensor_raw_t = t16;
	sensor_delta = raw24;
	sensor_ubar_raw = sensor_raw_to_ubar(raw24);
	sensor_sat = sensor_raw_saturated(raw24);

	mess_data.pressure = sensor_ubar_raw - EEPROM_values.offset;
	mess_data.temp = sensor_raw_to_temp(t16);

	last_good = mess_data;

	return mess_data;
}

#else	/* andere Variante: Datei traegt nichts bei */

/* Eine voellig leere Uebersetzungseinheit ist nach ISO C nicht zulaessig;
 * diese Deklaration haelt den Uebersetzer ruhig und erzeugt keinen Code. */
typedef int sensor_legacy_unused_t;

#endif
