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
 * Quelle: UM_WSEN-PDMS_25131308xxx05 (rev 1.0).
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
 * Byte-Reihenfolge: das Handbuch widerspricht sich - an einer Stelle
 * heisst es, das niederwertige Byte komme zuerst, an anderer, innerhalb
 * eines 16-Bit-Worts werde MSB zuerst uebertragen. Das laesst sich nur
 * am Aufbau klaeren.
 *
 * Probe am Prueffeld: die Temperatur muss bei Raumtemperatur rund
 * 2000..2500 (20..25 Grad C) liefern. Bei vertauschten Bytes liegt der
 * Wert voellig daneben (mehrere hundert Grad oder tief negativ). Trifft
 * das zu, diesen Schalter auf 0 setzen und neu bauen.
 */
#define SENSOR_PDMS_MSB_FIRST   1

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

	mess_data.pressure = sensor_raw_to_ubar(p16) - EEPROM_values.offset;
	mess_data.temp = sensor_raw_to_temp(t16);

	last_good = mess_data;

	return mess_data;
}

#else	/* andere Variante: Datei traegt nichts bei */

typedef int sensor_pdms_unused_t;

#endif
