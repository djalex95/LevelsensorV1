/*
 * sensor_pdms.c
 *
 * Einlesen des Wuerth WSEN-PDMS (2513130810x05) ueber I2C.
 * Wird bei HW_VARIANT=1001 (+/-10 kPa) und HW_VARIANT=1003 (+/-1 kPa)
 * mitkompiliert; der Unterschied steckt allein in SENSOR_FS_UBAR aus
 * sensor_cfg.h - der Sensor selbst verhaelt sich identisch.
 *
 * Anders als der alte Sensor laeuft dieser frei: er misst von sich aus
 * alle 2,4 ms und legt das Ergebnis in DSP_P/DSP_T ab. Es gibt also
 * keine Trigger-Sequenz, wir lesen einfach den aktuellen Stand.
 *
 * Quelle: Handbuch UM_WSEN-PDMS_25131308xxx05, Version 1.0,
 * November 2025. Die Fundstellen zu den Rohwerten stehen in
 * sensor_scale.h.
 */
#include "main.h"
#include "sensor.h"
#include "sensor_scale.h"

/* Wie sensor_legacy.c: der gesamte Inhalt haengt an der Variante, damit
 * die CubeIDE beide Treiberdateien uebersetzen kann, ohne dass doppelte
 * Symbole entstehen. */
#if SENSOR_KIND == SENSOR_KIND_PDMS

/* CubeMX-Handles und App-Zustand - definiert in main.c. */
extern I2C_HandleTypeDef hi2c1;
extern volatile uint8_t error_mode;
extern calib_data EEPROM_values;

#define SENSOR_ADDR8            (SENSOR_I2C_ADDR7 << 1)

/* Gemerkte Richtung, wenn der Druck den Nennbereich verlaesst. Ohne sie
 * schlaegt starker Unterdruck ab -32768 digits in vollen Ueberdruck um -
 * siehe sensor_range_delta() in sensor_scale.h. Genau ein Sensor, also
 * genau ein Zustand. */
static sensor_range_t press_range = { 0 };

/* Registeradressen laut Handbuch. */
#define PDMS_REG_CMD            0x22
#define PDMS_REG_DSP_T          0x2E    /* Temperatur, 16 Bit */
#define PDMS_REG_DSP_P          0x30    /* Druck, 16 Bit      */
#define PDMS_REG_STATUS_SYNC    0x32    /* Status, 16 Bit     */

/* Bits in STATUS_SYNC. */
#define PDMS_STAT_IDLE          (1U << 0)
#define PDMS_STAT_P_UP          (1U << 3)   /* neuer Druckwert verfuegbar */
#define PDMS_STAT_T_UP          (1U << 4)   /* neuer Temperaturwert       */
#define PDMS_STAT_CRC_ERR       (1U << 11)
#define PDMS_STAT_P_MISS        (1U << 14)
#define PDMS_STAT_T_MISS        (1U << 15)

#define PDMS_TIMEOUT_MS         25

/*
 * Byte-Reihenfolge: das Handbuch widerspricht sich.
 *
 * Abschnitt 4.3.7 (Read/Write operation, Seite 23): "The read/write data
 * is transferred MSB first - low byte before the high byte." Der Satz
 * meint zwei verschiedene Ebenen - die Bits gehen MSB zuerst, die Bytes
 * niederwertiges zuerst - und liest sich beim ersten Mal wie ein
 * Widerspruch in sich.
 *
 * Abschnitt 8.5 und 8.6 sagen das Gegenteil: dort soll das zuerst
 * uebertragene Byte das hoeherwertige sein. Dieselben Abschnitte
 * behaupten ausserdem, ab 0x2E kaeme zuerst der Druck und dann die
 * Temperatur - Abschnitt 8.2 sagt es umgekehrt. Uns beruehrt das nicht,
 * weil wir beide Register einzeln adressieren.
 *
 * Am Aufbau geklaert (V2-Board, Juli 2026), zugunsten von Abschnitt
 * 4.3.7: der Sensor sendet das NIEDERWERTIGE Byte zuerst. Mit MSB-first
 * gelesen zeigte die Firmware 210,19 Grad C (t16 = 0xE032) bei
 * tatsaechlich 20,64 Grad C (t16 = 0x32E0), und der Druck stand stabil
 * auf plus Vollausschlag (L = 99,9 % bei beiden offenen Ports), weil
 * das echte High-Byte konstant 0x40 ist und der vertauschte Wert damit
 * ueber SENSOR_OUT_MAX klemmt.
 *
 * Erkennungsmerkmal, falls das je wieder auftritt: die Temperatur
 * schwankt um rund 25 Grad C, sobald sich die echte Temperatur um
 * 0,1 Grad C aendert - das vertauschte Byte wirkt mit Faktor 256.
 */
#define SENSOR_PDMS_MSB_FIRST   0

/* Ein 16-Bit-Register lesen. Rueckgabe 1 = ok, 0 = I2C-Fehler. */
static uint8_t read_reg16(uint8_t reg, uint16_t *out)
{
	uint8_t rx[2] = {0, 0};

	if (HAL_I2C_Master_Transmit(&hi2c1, SENSOR_ADDR8, &reg, 1, PDMS_TIMEOUT_MS) != HAL_OK)
	{
		return 0;
	}
	if (HAL_I2C_Master_Receive(&hi2c1, SENSOR_ADDR8, rx, 2, PDMS_TIMEOUT_MS) != HAL_OK)
	{
		return 0;
	}

#if SENSOR_PDMS_MSB_FIRST
	*out = ((uint16_t)rx[0] << 8) | (uint16_t)rx[1];
#else
	*out = ((uint16_t)rx[1] << 8) | (uint16_t)rx[0];
#endif

	return 1;
}

void init_Sensor(void)
{
	uint16_t status = 0;
	uint8_t retries = 0;

	sensor_range_init(&press_range);

	/* Kein Startkommando noetig - der Sensor misst nach dem Anlegen der
	 * Versorgung selbstaendig weiter. Wir pruefen nur, ob er antwortet,
	 * und warten kurz auf den ersten gueltigen Messwert (2,4 ms Zyklus,
	 * dazu Reserve fuer den Anlauf). */
	if (HAL_I2C_IsDeviceReady(&hi2c1, SENSOR_ADDR8, 3, PDMS_TIMEOUT_MS) != HAL_OK)
	{
		error_mode |= ERROR_I2C;
		return;
	}

	while (retries++ < 20)
	{
		if (read_reg16(PDMS_REG_STATUS_SYNC, &status) == 0)
		{
			error_mode |= ERROR_I2C;
			return;
		}
		if (status & PDMS_STAT_P_UP)
		{
			return;		/* erster Messwert liegt vor */
		}
		HAL_Delay(5);
	}

	/* Der Sensor antwortet, liefert aber keinen Messwert. Kein
	 * ERROR_I2C - der Bus ist in Ordnung; get_value() meldet das
	 * Problem, falls es bestehen bleibt. */
}

sensor_mess get_value(void)
{
	static sensor_mess last_good = {0, 0};	/* letzter gueltiger Messwert */
	sensor_mess mess_data;
	uint16_t p16 = 0;
	uint16_t t16 = 0;

	/* Getrennte Registerzugriffe statt eines Blocklesens: das Handbuch
	 * sichert die automatische Adresserhoehung ueber Registergrenzen
	 * hinweg nicht ausdruecklich zu, und zwei kurze Zugriffe kosten bei
	 * 400 kHz nur wenige hundert Mikrosekunden. */
	if (read_reg16(PDMS_REG_DSP_P, &p16) == 0)
	{
		error_mode |= ERROR_I2C;
		return last_good;
	}
	if (read_reg16(PDMS_REG_DSP_T, &t16) == 0)
	{
		error_mode |= ERROR_I2C;
		return last_good;
	}

	/* Zwischenwerte fuer die Diagnose mitschreiben (Kommando 0x07).
	 * sensor_delta ist bewusst der ROHE, unbegrenzte Abstand zur Mitte
	 * ohne Bereichsueberwachung - nur daran ist im Protokoll zu sehen,
	 * wann das Register umlaeuft. Gerechnet wird mit dem ueberwachten
	 * Wert, der die Richtung beibehaelt. */
	sensor_raw_p = (int32_t)p16;
	sensor_raw_t = (int32_t)t16;
	sensor_delta = sensor_raw_delta(p16);
	sensor_ubar_raw = sensor_range_to_ubar(&press_range, p16);
	sensor_sat = (press_range.dir == 0) ? 0u
	           : ((press_range.dir < 0) ? 2u : 1u);

	mess_data.pressure = sensor_ubar_raw - EEPROM_values.offset;
	mess_data.temp = sensor_raw_to_temp(t16);

	last_good = mess_data;

	return mess_data;
}

#else	/* andere Variante: Datei traegt nichts bei */

typedef int sensor_pdms_unused_t;

#endif
