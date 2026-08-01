/*
 * test_sensor_conv.c
 *
 * Host-Test der Rohwert-Umrechnung aus Core/Inc/sensor_scale.h.
 * Laeuft auf dem PC, ohne HAL und ohne Zielhardware - deshalb ist die
 * Skalierung bewusst in einem eigenen, HAL-freien Header abgelegt.
 *
 * Der Test wird einmal je Variante uebersetzt, weil die Umrechnung ueber
 * Praeprozessorkonstanten ausgewaehlt wird:
 *
 *   gcc -std=gnu11 -Wall -Wextra -ICore/Inc -ICore/Inc/variants/1000 \
 *       -o /tmp/t1000 tests/test_sensor_conv.c && /tmp/t1000
 *   gcc -std=gnu11 -Wall -Wextra -ICore/Inc -ICore/Inc/variants/1001 \
 *       -o /tmp/t1001 tests/test_sensor_conv.c && /tmp/t1001
 *   gcc -std=gnu11 -Wall -Wextra -ICore/Inc -ICore/Inc/variants/1003 \
 *       -o /tmp/t1003 tests/test_sensor_conv.c && /tmp/t1003
 *
 * (tests/run_tests.sh macht genau das der Reihe nach.)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "sensor_scale.h"

static int fails = 0;
static int checks = 0;

static void check_i32(const char *what, int32_t got, int32_t want)
{
	checks++;
	if (got != want)
	{
		fails++;
		printf("  FEHLER %-38s ist %ld, erwartet %ld\n",
		       what, (long)got, (long)want);
	}
}

#if SENSOR_KIND == SENSOR_KIND_PDMS

/* Referenzrechnung in 64 Bit, direkt nach der Datenblattformel
 *   Druck = (P16 - OUTP_MIN) * SENP + PMIN
 * mit SENP = 2*FS / (OUTP_MAX - OUTP_MIN) und PMIN = -FS.
 * Rohwerte ab 49152 gehoeren zum negativen Ast (der DSP rechnet bei
 * Unterdruck unter null weiter, das Registerfeld ist vorzeichenlos)
 * und werden vorher zurueckgefaltet.
 * Muss mit der int32-Mittelpunktsform aus sensor_scale.h uebereinstimmen. */
static int32_t ref_ubar(uint16_t p16)
{
	int64_t p = (int64_t)p16;

	if (p >= (int64_t)SENSOR_OUT_MID + 32768) { p -= 65536; }

	if (p < SENSOR_OUT_MIN)      { p = SENSOR_OUT_MIN; }
	else if (p > SENSOR_OUT_MAX) { p = SENSOR_OUT_MAX; }

	return (int32_t)(((p - SENSOR_OUT_MID) * (int64_t)SENSOR_FS_UBAR)
	                 / (int64_t)SENSOR_OUT_HALFSPAN);
}

static void test_pressure(void)
{
	printf("Druck (WSEN-PDMS, Vollausschlag %ld uBar):\n", (long)SENSOR_FS_UBAR);

	check_i32("Mitte 16384 -> 0 uBar",
	          sensor_raw_to_ubar(16384), 0);
	check_i32("Untergrenze 3277 -> -Vollausschlag",
	          sensor_raw_to_ubar(SENSOR_OUT_MIN), -SENSOR_FS_UBAR);
	check_i32("Obergrenze 29491 -> +Vollausschlag",
	          sensor_raw_to_ubar(SENSOR_OUT_MAX), SENSOR_FS_UBAR);

	/* Unterhalb/oberhalb des Bereichs wird begrenzt, nicht extrapoliert. */
	check_i32("Rohwert 0 wird begrenzt",
	          sensor_raw_to_ubar(0), -SENSOR_FS_UBAR);
	check_i32("Rohwert 32767 wird begrenzt",
	          sensor_raw_to_ubar(32767), SENSOR_FS_UBAR);

	/* Starker Unterdruck: der DSP rechnet unter null weiter, im
	 * vorzeichenlosen Registerfeld erscheinen sehr grosse Zahlen. Die
	 * duerfen nicht als voller Ueberdruck durchgehen. */
	check_i32("Rohwert 65535 ist Unterdruck",
	          sensor_raw_to_ubar(65535), -SENSOR_FS_UBAR);
	check_i32("Rohwert 60000 ist Unterdruck",
	          sensor_raw_to_ubar(60000), -SENSOR_FS_UBAR);
	check_i32("Umschlagpunkt 49152 ist Unterdruck",
	          sensor_raw_to_ubar(49152), -SENSOR_FS_UBAR);
	check_i32("49151 ist noch Ueberdruck",
	          sensor_raw_to_ubar(49151), SENSOR_FS_UBAR);

	check_i32("Saettigung bei 3276 gemeldet",
	          sensor_raw_saturated(SENSOR_OUT_MIN - 1), 1);
	check_i32("Saettigung bei 29492 gemeldet",
	          sensor_raw_saturated(SENSOR_OUT_MAX + 1), 1);
	check_i32("Saettigung bei 65535 gemeldet",
	          sensor_raw_saturated(65535), 1);
	check_i32("keine Saettigung im Bereich",
	          sensor_raw_saturated(16384), 0);

	/* Symmetrie: gleicher Abstand von der Mitte -> gleicher Betrag. */
	check_i32("Symmetrie +/-6553 digits",
	          sensor_raw_to_ubar(16384 + 6553) + sensor_raw_to_ubar(16384 - 6553), 0);

	/* Monotonie und Uebereinstimmung mit der 64-Bit-Referenz ueber den
	 * gesamten Rohwertebereich. Durchlaufen wird nach dem Abstand zur
	 * Bereichsmitte, weil die Kennlinie darueber monoton ist und nicht
	 * ueber dem vorzeichenlosen Rohwert (der bei Unterdruck umlaeuft).
	 * Das faengt einen Ueberlauf der int32-Multiplikation zuverlaessig
	 * ab: er wuerde als Sprung auffallen. */
	{
		int32_t prev = sensor_raw_to_ubar((uint16_t)(SENSOR_OUT_MID - 32768));
		int      mono_ok = 1;
		int      ref_ok = 1;
		int32_t  d;

		for (d = -32768; d <= 32767; d++)
		{
			uint16_t raw = (uint16_t)(SENSOR_OUT_MID + d);
			int32_t  v   = sensor_raw_to_ubar(raw);

			if (v < prev)            { mono_ok = 0; }
			if (v != ref_ubar(raw))  { ref_ok = 0; }
			prev = v;
		}
		check_i32("monoton steigend ueber alle 65536 Rohwerte", mono_ok, 1);
		check_i32("deckungsgleich mit 64-Bit-Referenzformel", ref_ok, 1);
	}

	/* Kein Ueberlauf: groesstes Zwischenprodukt der int32-Rechnung. */
	check_i32("Zwischenprodukt passt in int32",
	          (int32_t)((int64_t)SENSOR_OUT_HALFSPAN * SENSOR_FS_UBAR < 2147483647LL),
	          1);
}

static void test_temperature(void)
{
	printf("Temperatur (WSEN-PDMS):\n");

	check_i32("8192 -> 0,00 Grad C",  sensor_raw_to_temp(8192), 0);
	/* Datenblatt: 24576 entspricht 70 Grad C; die Ganzzahlrechnung
	 * liefert 69,99 - der Rundungsfehler ist mit 0,01 Grad belanglos. */
	check_i32("24576 -> 69,99 Grad C", sensor_raw_to_temp(24576), 6999);
	check_i32("13342 -> 22,00 Grad C", sensor_raw_to_temp(13342), 2200);
	/* Unterhalb 8192 wird es negativ - Vorzeichen muss erhalten bleiben. */
	check_i32("0 -> -34,99 Grad C",    sensor_raw_to_temp(0), -3499);

	/* Der Rueckgabetyp ist int16: der Wertebereich des Sensors
	 * (-40..+125 Grad C) passt mit -4000..12500 problemlos hinein. */
	check_i32("Rueckgabe passt in int16 (Maximalwert)",
	          (int32_t)(sensor_raw_to_temp(24576) == 6999), 1);
}

#else	/* SENSOR_KIND_LEGACY */

static void test_pressure(void)
{
	printf("Druck (alter 24-Bit-Sensor):\n");

	check_i32("0 -> 0 uBar",             sensor_raw_to_ubar(0), 0);
	check_i32("12800 -> 1000 uBar",      sensor_raw_to_ubar(12800), 1000);
	check_i32("-12800 -> -1000 uBar",    sensor_raw_to_ubar(-12800), -1000);
	check_i32("128 -> 10 uBar",          sensor_raw_to_ubar(128), 10);

	/* Groesster moeglicher Rohwert des 24-Bit-Zweierkomplements. */
	check_i32("8388607 -> 655359 uBar",  sensor_raw_to_ubar(8388607), 655359);
	check_i32("-8388608 -> -655360 uBar", sensor_raw_to_ubar(-8388608), -655360);

	check_i32("keine Saettigungsmeldung", sensor_raw_saturated(8388607), 0);
}

static void test_temperature(void)
{
	printf("Temperatur (alter 24-Bit-Sensor):\n");

	check_i32("0 -> 0,00 Grad C",     sensor_raw_to_temp(0), 0);
	check_i32("256 -> 1,00 Grad C",   sensor_raw_to_temp(256), 100);
	check_i32("5632 -> 22,00 Grad C", sensor_raw_to_temp(5632), 2200);
	check_i32("-2560 -> -10,00 Grad C", sensor_raw_to_temp(-2560), -1000);
}

#endif

int main(void)
{
	printf("=== sensor_scale.h, Variante: %s ===\n", SENSOR_NAME);

	test_pressure();
	test_temperature();

	printf("--- %d Pruefungen, %d Fehler ---\n\n", checks, fails);

	return (fails == 0) ? 0 : 1;
}
