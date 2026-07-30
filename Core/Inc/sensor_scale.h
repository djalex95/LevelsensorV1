/*
 * sensor_scale.h
 *
 * Umrechnung Rohwert -> physikalische Groesse, getrennt vom I2C-Zugriff.
 * Bewusst frei von HAL-Abhaengigkeiten, damit die Skalierung auf dem PC
 * getestet werden kann (tests/test_sensor_conv.c). Die messbereichs-
 * abhaengigen Zahlen kommen aus der Variante (sensor_cfg.h).
 *
 * Einheiten wie bisher:
 *   Druck       in uBar     (1 uBar = 0,1 Pa)
 *   Temperatur  in 0,01 Grad C
 *
 * Alles in Ganzzahlarithmetik - der Cortex-M0+ hat keine FPU, jede
 * Fliesskommarechnung zoege die Soft-Float-Bibliothek in den Flash.
 */
#ifndef INC_SENSOR_SCALE_H_
#define INC_SENSOR_SCALE_H_

#include <stdint.h>
#include "sensor_cfg.h"

#if SENSOR_KIND == SENSOR_KIND_PDMS

/* --- Wuerth WSEN-PDMS -------------------------------------------------
 *
 * Handbuch (UM_WSEN-PDMS, rev 1.0), bidirektionale Typen (+/-1, +/-10,
 * +/-35 kPa):
 *
 *   Druck [kPa]      = (P16 - OUTP_MIN) * SENP + PMIN
 *   Temperatur [GrdC]= (T16 - OUTT_MIN) * SENT
 *
 * mit OUTP_MIN = 3277, OUTP_MAX = 29491, OUTT_MIN = 8192 und
 * SENT = 4,272e-3 GrdC/digit.
 *
 * Wir rechnen die Druckformel um die Bereichsmitte herum, weil das
 * Ergebnis identisch ist, aber ohne 64-Bit-Multiplikation auskommt:
 *
 *   Mitte  = (OUTP_MIN + OUTP_MAX) / 2 = 16384  -> 0 kPa
 *   Halbe Spanne = (OUTP_MAX - OUTP_MIN) / 2 = 13107
 *   Druck [uBar] = (P16 - 16384) * FS_UBAR / 13107
 *
 * Probe: P16 = 3277 -> -FS_UBAR, P16 = 29491 -> +FS_UBAR.
 * Die direkte Form (P16 - 3277) * 2*FS_UBAR / 26214 wuerde beim
 * +/-10-kPa-Typ mit 26214 * 200000 = 5,2e9 den int32 sprengen.
 */
#define SENSOR_OUT_MIN        3277
#define SENSOR_OUT_MAX        29491
#define SENSOR_OUT_MID        16384      /* Rohwert bei 0 kPa            */
#define SENSOR_OUT_HALFSPAN   13107      /* Rohwerte je Richtung         */
#define SENSOR_OUTT_MIN       8192       /* Rohwert bei 0 Grad C         */

/* Rohwert -> Druck in uBar. Der Rohwert wird auf den gueltigen Bereich
 * begrenzt: das haelt die Multiplikation im int32 und liefert bei
 * Ueberlast einen sauber gesaettigten Wert statt eines Ueberlaufs. */
static inline int32_t sensor_raw_to_ubar(uint16_t p16)
{
	int32_t p = (int32_t)p16;

	if (p < SENSOR_OUT_MIN)
	{
		p = SENSOR_OUT_MIN;
	}
	else if (p > SENSOR_OUT_MAX)
	{
		p = SENSOR_OUT_MAX;
	}

	return ((p - SENSOR_OUT_MID) * SENSOR_FS_UBAR) / SENSOR_OUT_HALFSPAN;
}

/* 1, wenn der Rohwert ausserhalb des spezifizierten Bereichs lag (der
 * Messwert ist dann gesaettigt und nicht mehr vertrauenswuerdig). */
static inline uint8_t sensor_raw_saturated(uint16_t p16)
{
	return (uint8_t)((p16 < SENSOR_OUT_MIN) || (p16 > SENSOR_OUT_MAX));
}

/* Rohwert -> Temperatur in 0,01 Grad C.
 * SENT = 4,272e-3 GrdC/digit -> in 0,01 GrdC: * 0,4272 = * 267/625
 * (der Bruch ist exakt, 4272/10000 gekuerzt).
 * Probe: T16 = 24576 -> 6999, also 69,99 GrdC (Datenblatt: 70 GrdC). */
static inline int16_t sensor_raw_to_temp(uint16_t t16)
{
	return (int16_t)((((int32_t)t16 - SENSOR_OUTT_MIN) * 267) / 625);
}

#elif SENSOR_KIND == SENSOR_KIND_LEGACY

/* --- Alter 24-Bit-Drucksensor ----------------------------------------
 *
 * Druck: raw / k mit k = 12,8 (40-kPa-Typ) -> raw * 10 / 128.
 *        (20-kPa-Typ waere k = 25,6 -> raw * 10 / 256.)
 * Temperatur: raw / 256 Grad C -> in 0,01 Grad C: raw * 100 / 256
 *        = raw * 25 / 64.
 *
 * Der Wertebereich von raw24 ist +/-8388608; mal 10 sind das hoechstens
 * 8,4e7 und damit sicher im int32. Die frueher benutzte int64-Rechnung
 * war fuer jeden moeglichen Eingabewert identisch, kostete aber einen
 * Aufruf der 64-Bit-Divisionsroutine.
 */
static inline int32_t sensor_raw_to_ubar(int32_t raw24)
{
	return (raw24 * 10) / 128;
}

static inline uint8_t sensor_raw_saturated(int32_t raw24)
{
	(void)raw24;
	return 0;	/* der alte Sensor meldet keine Bereichsueberschreitung */
}

static inline int16_t sensor_raw_to_temp(int32_t t16)
{
	return (int16_t)((t16 * 25) / 64);
}

#else
#error "SENSOR_KIND ist nicht gesetzt - fehlt der Include-Pfad Core/Inc/variants/<HW_VARIANT>?"
#endif

#endif /* INC_SENSOR_SCALE_H_ */
