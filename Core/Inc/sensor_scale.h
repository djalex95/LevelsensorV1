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
 * Handbuch: UM_WSEN-PDMS_25131308xxx05, Version 1.0 (November 2025).
 * Fundstellen, die hier eingerechnet sind:
 *
 *   Abschnitt 8.3, Tabelle 21   Digitalausgang Druck, bidirektionale
 *                               Typen: OUTOFF 16384, FSS 26214,
 *                               OUTP_MIN 3277, OUTP_MAX 29491 digits.
 *                               Beide bestueckten Typen stehen dort:
 *                               2513130810105 (+/-1 kPa, Variante 1003)
 *                               und ...0205 (+/-10 kPa, Variante 1001).
 *                               Tabelle 22 und 23 gelten fuer die
 *                               unidirektionalen Typen mit anderem
 *                               Nullpunkt - nicht verwechseln.
 *   Abschnitt 8.4, Tabelle 24   Digitalausgang Temperatur, fuer alle
 *                               Typen: OUTT_MIN 8192, OUTT_MAX 24576.
 *   Tabelle 11                  SENT = 4,272e-3 GrdC/digit.
 *   Abschnitt 10, Tabelle 28    SENP je Bestellnummer: 7,63e-5 kPa/digit
 *                               (+/-1 kPa) und 7,63e-4 (+/-10 kPa).
 *   Abschnitt 8.5 / 8.6         die Formeln selbst:
 *
 *   Druck [kPa]      = (P16 - OUTP_MIN) * SENP + PMIN
 *   Temperatur [GrdC]= (T16 - OUTT_MIN) * SENT
 *
 * Wichtig ist der Nullpunkt: bei den bidirektionalen Typen liegt 0 kPa
 * nicht beim Rohwert 0, sondern bei OUTOFF = 16384 - Nennbereich also
 * 3277..29491 statt 0..32768. Das Handbuch nennt diesen Wert in
 * Tabelle 21 ausdruecklich; er ist zugleich die Mitte von OUTP_MIN und
 * OUTP_MAX.
 *
 * Wir rechnen die Druckformel um diese Mitte herum, weil das Ergebnis
 * identisch ist, aber ohne 64-Bit-Multiplikation auskommt:
 *
 *   Mitte  = OUTOFF = (OUTP_MIN + OUTP_MAX) / 2 = 16384  -> 0 kPa
 *   Halbe Spanne = FSS / 2 = (OUTP_MAX - OUTP_MIN) / 2 = 13107
 *   Druck [uBar] = (P16 - 16384) * FS_UBAR / 13107
 *
 * Probe: P16 = 3277 -> -FS_UBAR, P16 = 29491 -> +FS_UBAR.
 * Zweite Probe gegen Tabelle 28: 13107 digits * 7,63e-5 kPa/digit
 * = 1,0001 kPa, also der Vollausschlag des +/-1-kPa-Typs.
 * Die direkte Form (P16 - 3277) * 2*FS_UBAR / 26214 wuerde beim
 * +/-10-kPa-Typ mit 26214 * 200000 = 5,2e9 den int32 sprengen.
 */
#define SENSOR_OUT_MIN        3277
#define SENSOR_OUT_MAX        29491
#define SENSOR_OUT_MID        16384      /* OUTOFF, Rohwert bei 0 kPa    */
#define SENSOR_OUT_HALFSPAN   13107      /* FSS/2, Rohwerte je Richtung  */
#define SENSOR_OUTT_MIN       8192       /* OUTT_MIN, Rohwert bei 0 Grd C*/

/* Abstand des Rohwerts zur Bereichsmitte, vorzeichenbehaftet.
 *
 * Bei Unterdruck jenseits des Nennbereichs rechnet der DSP unter null
 * weiter, das Registerfeld ist aber vorzeichenlos: aus -1 wird 65535.
 * Im Handbuch steht dazu nichts - es beschreibt nur den Nennbereich und
 * sagt weder, dass der Wert begrenzt wird, noch, dass er weiterlaeuft.
 * Am Aufbau gemessen laeuft er weiter.
 * Wuerde man den Rohwert direkt begrenzen, landete so ein Wert an der
 * Obergrenze und der Sensor meldete ploetzlich vollen Ueberdruck.
 *
 * Die Subtraktion wird deshalb modulo 65536 gerechnet und das Ergebnis
 * als vorzeichenbehaftete 16-Bit-Zahl gelesen. Damit stimmt der Abstand
 * fuer alles zwischen -32768 und +32767 digits, also bis etwa dem
 * 2,5-fachen Vollausschlag in beide Richtungen. Danach ist Schluss: bei
 * p16 = 49152 laeuft der Wert von -32768 auf +32767 um, und der Sensor
 * meldete bis Version 2.0.0 vollen Ueberdruck statt vollem Unterdruck.
 *
 * Am Aufbau gemessen (August 2026, Variante 1003, Vollausschlag
 * +/-10 mBar): der letzte richtige Wert lag bei p16 = 50672 mit -31248
 * digits, der erste falsche bei p16 = 48626 mit scheinbar +32242 digits
 * und einer gemeldeten Fuellhoehe von 99,50 %. Aus einem einzelnen
 * Registerwert ist das nicht mehr aufzuloesen - dafuer gibt es
 * sensor_range_delta() weiter unten.
 *
 * Die Fallunterscheidung statt eines Casts nach int16_t: die Umwandlung
 * eines zu grossen vorzeichenlosen Werts in einen vorzeichenbehafteten
 * Typ ist in C nicht festgelegt. Der Compiler macht daraus ohnehin
 * dieselben ein bis zwei Befehle. */
static inline int32_t sensor_raw_delta(uint16_t p16)
{
	uint16_t u = (uint16_t)(p16 - (uint16_t)SENSOR_OUT_MID);

	return (u >= 32768u) ? ((int32_t)u - 65536) : (int32_t)u;
}

/* Abstand zur Mitte -> Druck in uBar. Der Abstand wird auf die halbe
 * Nennspanne begrenzt: das haelt die Multiplikation im int32 und
 * liefert bei Ueberlast einen sauber gesaettigten Wert statt eines
 * Ueberlaufs - in der Richtung, aus der die Ueberlast kam. */
static inline int32_t sensor_delta_to_ubar(int32_t d)
{
	if (d < -SENSOR_OUT_HALFSPAN)
	{
		d = -SENSOR_OUT_HALFSPAN;
	}
	else if (d > SENSOR_OUT_HALFSPAN)
	{
		d = SENSOR_OUT_HALFSPAN;
	}

	return (d * SENSOR_FS_UBAR) / SENSOR_OUT_HALFSPAN;
}

/* Rohwert -> Druck in uBar, ohne Gedaechtnis. Richtig fuer alles bis
 * +/-32767 digits Abstand zur Mitte; jenseits davon siehe
 * sensor_range_delta(). */
static inline int32_t sensor_raw_to_ubar(uint16_t p16)
{
	return sensor_delta_to_ubar(sensor_raw_delta(p16));
}

/* 1, wenn der Rohwert ausserhalb des spezifizierten Bereichs lag (der
 * Messwert ist dann gesaettigt und nicht mehr vertrauenswuerdig). */
static inline uint8_t sensor_raw_saturated(uint16_t p16)
{
	int32_t d = sensor_raw_delta(p16);

	return (uint8_t)((d < -SENSOR_OUT_HALFSPAN) || (d > SENSOR_OUT_HALFSPAN));
}

/* --- Bereichsueberwachung mit Gedaechtnis ----------------------------
 *
 * Der Umlauf bei p16 = 49152 ist aus einem einzelnen Messwert nicht zu
 * erkennen: -33294 digits und +32242 digits stehen beide als 32242 im
 * Register. Welcher der beiden gemeint ist, ergibt sich nur aus dem
 * Weg dorthin.
 *
 * Der Ausweg braucht kein Nachverfolgen des genauen Werts, denn
 * ausserhalb des Nennbereichs ist der Betrag ohnehin ohne Aussage - der
 * Messwert wird dort auf den Vollausschlag begrenzt. Gebraucht wird nur
 * die RICHTUNG, und die aendert sich nicht, solange der Sensor
 * ausserhalb bleibt:
 *
 *   - Liegt der Rohwert im Nennbereich (3277..29491), stimmt das
 *     Vorzeichen. Der Wert wird unveraendert benutzt und die gemerkte
 *     Richtung geloescht.
 *   - Verlaesst er den Nennbereich, wird die Richtung beim ERSTEN
 *     Schritt nach draussen festgehalten. Dort ist der Abstand zur
 *     Mitte gerade eben groesser als 13107 digits und damit noch weit
 *     vom Umlauf bei 32768 entfernt, also verlaesslich.
 *   - Bleibt er draussen, wird das Vorzeichen des Rohwerts nicht mehr
 *     ausgewertet. Gemeldet wird der Vollausschlag der gemerkten
 *     Richtung - egal, wie oft der Registerwert noch umlaeuft.
 *
 * Zurueck in den Nennbereich kommt der Sensor nur ueber die Grenze,
 * durch die er hinausgegangen ist, denn dazwischen liegen keine
 * Rohwerte. Ein Wechsel von vollem Unterdruck auf vollen Ueberdruck
 * ohne einen einzigen Messwert dazwischen ist bei 2,4 ms Messtakt
 * ausgeschlossen.
 *
 * Offen bleibt der Sonderfall, dass die Firmware startet, waehrend der
 * Sensor schon jenseits des Umlaufs liegt. Dann fehlt die Vorgeschichte
 * und die erste Richtung kann falsch herum sein. Sobald der Druck
 * einmal in den Nennbereich zurueckkehrt, stimmt es wieder.
 *
 * Der Zustand steht bewusst in einer Struktur beim Aufrufer und nicht
 * als static in diesem Header: sonst gaebe es ihn in jeder Datei, die
 * den Header einbindet, ein weiteres Mal. So laesst er sich ausserdem
 * im Host-Test setzen und pruefen. */
typedef struct
{
	int8_t dir;	/* 0 = im Nennbereich, -1 = Unterdruck, +1 = Ueberdruck */
} sensor_range_t;

static inline void sensor_range_init(sensor_range_t *st)
{
	st->dir = 0;
}

/* Abstand zur Bereichsmitte, gegen den Umlauf abgesichert. Ausserhalb
 * des Nennbereichs wird knapp jenseits der Grenze zurueckgegeben, damit
 * die nachfolgende Begrenzung genau den Vollausschlag liefert. */
static inline int32_t sensor_range_delta(sensor_range_t *st, uint16_t p16)
{
	int32_t d = sensor_raw_delta(p16);

	if ((d >= -SENSOR_OUT_HALFSPAN) && (d <= SENSOR_OUT_HALFSPAN))
	{
		st->dir = 0;
		return d;
	}

	if (st->dir == 0)
	{
		st->dir = (d < 0) ? -1 : 1;
	}

	return (st->dir < 0) ? (-SENSOR_OUT_HALFSPAN - 1)
	                     : (SENSOR_OUT_HALFSPAN + 1);
}

/* Rohwert -> Druck in uBar, mit Bereichsueberwachung. Das ist der Weg,
 * den der Treiber nimmt; sensor_raw_to_ubar() bleibt fuer Stellen ohne
 * Zustand (Tests, Umrechnungen im Werkzeug). */
static inline int32_t sensor_range_to_ubar(sensor_range_t *st, uint16_t p16)
{
	return sensor_delta_to_ubar(sensor_range_delta(st, p16));
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
