/*
 * ble_app.c
 *
 * BLE-Kommando-Ebene der Applikation (siehe ble_app.h).
 * Aus main.c herausgezogen; Verhalten unveraendert.
 */
#include "main.h"
#include "ble_app.h"
#include "app_types.h"
#include "app_config.h"
#include "config_store.h"
#include "dfu_common.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* App-Zustand - definiert in main.c. */
extern NMEA_parameter_Device dev_info_par;
extern prod_param device_param;
extern calib_data EEPROM_values;
extern sensor_mess sensor_data_rx;
extern char sensor_name[CFG_NAME_LEN + 1];
extern uint16_t percent_val;
extern volatile int32_t raw_press;
extern volatile uint8_t dev_info;

/* Gewuenschter BLE-Modulname (max. 20 Zeichen, Proteus-Limit):
 * der Sensorname aus dem Config ist die einzige Quelle der Wahrheit.
 * Ist keiner gesetzt (Werkszustand), gilt der Default "LevelSense-<UID>"
 * aus der NMEA2000 Unique Number - damit ist jede Platine ab Werk
 * eindeutig unterscheidbar. buf braucht mind. 21 Bytes. */
void ble_desired_name(char *buf)
{
	if (sensor_name[0] != '\0')
	{
		strncpy(buf, sensor_name, 20);
		buf[20] = '\0';
	}
	else
	{
		/* 21-bit Unique Number -> max. 6 Hex-Zeichen, gesamt <= 17 Zeichen */
		snprintf(buf, 21, "LevelSense-%05lX", (unsigned long)dev_info_par.UniqueNumber);
	}
}

/*
 * Sendet den aktuellen Sensorzustand als maschinenlesbare Zeile an die App.
 * Format:  STAT;L=<%>;T=<C>;F=<typ>;C=<L>;I=<inst>;CAL=<0/1>;V=<x.y.z>;HW=<rev>\n
 * L: Füllstand in %, T: Temperatur in Grad C, F: Fluidtyp (0..15),
 * C: Kapazität (Liter), I: Instanz, CAL: 1 = kalibriert.
 */
void ble_send_status(void)
{
	char line[96];

	/* percent_val: 100,00 % = 10000 */
	int p_int = percent_val / 100;
	int p_frac = (percent_val % 100) / 10;

	/* temp: 0,01 Grad C, Vorzeichen sauber behandeln */
	int16_t t = sensor_data_rx.temp;
	const char *tsign = (t < 0) ? "-" : "";
	int ta = (t < 0) ? -t : t;

	int cal = (EEPROM_values.calib_available == 0x00) ? 1 : 0;

	snprintf(line, sizeof(line), "STAT;L=%d.%d;T=%s%d.%02d;F=%d;C=%d;I=%d;CAL=%d;V=%s;HW=%d\n",
			 p_int, p_frac, tsign, ta / 100, ta % 100,
			 dev_info_par.fluidType, dev_info_par.cap, dev_info_par.devInstance, cal,
			 FW_VERSION, HW_REV);

	BLE_SendString(line);
}

/* Sendet die aktuelle Tankform-Kennlinie:  LIN;v0,v1,...,v10\n */
static void ble_send_lin(void)
{
	char line[64];
	int n = 0;
	n += snprintf(line + n, sizeof(line) - n, "LIN;");
	for (int i = 0; i < 11; i++)
	{
		n += snprintf(line + n, sizeof(line) - n, "%d%s",
					  device_param.lin_point[i], (i < 10) ? "," : "\n");
	}
	BLE_SendString(line);
}

/*
 * Verarbeitet ein Textkommando von der App (CMD_DATA_IND). Vollstaendige
 * Spezifikation in PC_Tools/BLE_Protokoll.md. Unterstützt (case-insensitive):
 *   VER            Firmware-Version senden (VER;x.y.z)
 *   GET            aktuellen Status sofort senden (STAT;...)
 *   LIN            aktuelle Tankform-Kennlinie senden (LIN;...)
 *   LIN v0,...,v10 Kennlinie setzen (11 Werte 0..100, steigend)
 *   CAL100         aktuellen Druck als 100 % kalibrieren
 *   CALRESET       Kalibrierung auf Werkswert zurücksetzen
 *   FLUID <0..15>  Fluidtyp setzen
 *   CAP <1..255>   Tankkapazität (Liter) setzen
 *   INST <0..15>   Instanz setzen
 *   NAME <text>    Sensor-/BLE-Namen setzen; NAME (ohne Arg) fragt ihn ab
 *   FACTORYRESET   Config löschen und neu starten
 *   DFU            in den Bootloader/Update-Modus wechseln
 */
void ble_handle_command(const uint8_t *data, uint16_t len)
{
	char cmd[80];
	char resp[32];
	uint16_t n = (len < sizeof(cmd) - 1) ? len : sizeof(cmd) - 1;
	memcpy(cmd, data, n);
	cmd[n] = '\0';

	/* trailing CR/LF/Leerzeichen entfernen */
	while (n > 0 && (cmd[n-1] == '\r' || cmd[n-1] == '\n' || cmd[n-1] == ' '))
	{
		cmd[--n] = '\0';
	}

	if (strncasecmp(cmd, "VER", 3) == 0)
	{
		BLE_SendString("VER;" FW_VERSION "\n");
	}
	else if (strncasecmp(cmd, "GET", 3) == 0)
	{
		ble_send_status();
	}
	else if (strncasecmp(cmd, "LIN", 3) == 0)
	{
		if (cmd[3] == '\0')				/* Abfrage der Kennlinie */
		{
			ble_send_lin();
		}
		else if (cmd[3] == ' ')			/* Kennlinie setzen: LIN v0,..,v10 */
		{
			uint8_t pts[11];
			int cnt = 0;
			char *p = cmd + 4;
			while (cnt < 11 && *p)
			{
				pts[cnt++] = (uint8_t)atoi(p);
				while (*p && *p != ',') p++;
				if (*p == ',') p++;
			}
			if (cnt == 11 && lin_table_valid(pts))
			{
				memcpy(device_param.lin_point, pts, 11);
				set_param_eeprom(&dev_info_par, &device_param);
				BLE_SendString("OK LIN\n");
			}
			else
			{
				BLE_SendString("ERR LIN\n");
			}
		}
		else
		{
			BLE_SendString("ERR ?\n");
		}
	}
	else if (strncasecmp(cmd, "CAL100", 6) == 0)
	{
		if (raw_press >= 100)	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz) */
		{
			EEPROM_values.max_val = raw_press / 100;
			EEPROM_values.calib_available = 0x00;
			save_EEPROM(&EEPROM_values);
			BLE_SendString("OK CAL100\n");
		}
		else
		{
			BLE_SendString("ERR CAL100 nodruck\n");
		}
	}
	else if (strncasecmp(cmd, "CALRESET", 8) == 0)
	{
		EEPROM_values.calib_available = 0xFF;
		save_EEPROM(&EEPROM_values);
		EEPROM_values.max_val = std_press;
		BLE_SendString("OK CALRESET\n");
	}
	else if (strncasecmp(cmd, "FACTORYRESET", 12) == 0)
	{
		/* Kompletten Config loeschen und neu starten (Adresse 0x21,
		 * unkalibriert, kein Name). Der BLE-Modulname faellt beim naechsten
		 * Boot per Namensabgleich auf "LevelSense-<UID>" zurueck. */
		BLE_SendString("OK FACTORYRESET\n");
		HAL_Delay(100);
		config_factory_reset();
		__disable_irq();
		NVIC_SystemReset();
	}
	else if (strncasecmp(cmd, "DFU", 3) == 0)
	{
		/* Update-Modus anfordern: Magic ins reservierte RAM schreiben und neu
		 * starten. Der Bootloader erkennt es und geht in den Empfangsmodus. */
		BLE_SendString("OK DFU\n");
		HAL_Delay(100);
		*DFU_REQ_ADDR = DFU_REQ_MAGIC;
		__disable_irq();
		NVIC_SystemReset();
	}
	else if ((strncasecmp(cmd, "NAME", 4) == 0) && (cmd[4] == '\0'))
	{
		/* Abfrage: gespeicherten Sensornamen melden (NAME;<text>).
		 * Noetig, weil BLE-Modulname und Sensorname auseinanderlaufen
		 * koennen (Name per Group Function vom Plotter gesetzt). */
		snprintf(resp, sizeof(resp), "NAME;%s\n", sensor_name);
		BLE_SendString(resp);
	}
	else if (strncasecmp(cmd, "NAME ", 5) == 0)
	{
		const char *name = cmd + 5;
		if (*name != '\0')
		{
			if (strcmp(name, sensor_name) == 0)
			{
				/* unveraendert -> kein Flash-Schreibzugriff, kein
				 * Modul-Neustart (die Verbindung bleibt bestehen) */
				BLE_SendString("OK NAME\n");
			}
			else
			{
				char want[21];
				/* Name persistent speichern -> erscheint als Installation
				 * Description in PGN 126998 (Geraeteliste am Plotter). */
				set_name_eeprom(name);
				get_name_eeprom(sensor_name);
				dev_info++;		/* aktualisiertes 126998 auf dem NMEA-Bus
								 * verschicken (Symmetrie zum Setzen per
								 * Group Function: PC-Tool/Plotter sehen den
								 * neuen Namen sofort) */
				/* erst bestätigen, dann Modul umbenennen (Modul startet danach
				 * neu und trennt die Verbindung). Der Modulname folgt dem
				 * gespeicherten (ggf. auf 24 Zeichen gekuerzten) Sensornamen. */
				BLE_SendString("OK NAME\n");
				HAL_Delay(50);
				ble_desired_name(want);
				BLE_SetDeviceName(want);
			}
		}
		else
		{
			BLE_SendString("ERR NAME\n");
		}
	}
	else if (strncasecmp(cmd, "FLUID ", 6) == 0)
	{
		int v = atoi(cmd + 6);
		if (v >= 0 && v <= 15)
		{
			dev_info_par.fluidType = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK FLUID %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR FLUID\n");
	}
	else if (strncasecmp(cmd, "CAP ", 4) == 0)
	{
		int v = atoi(cmd + 4);
		if (v >= 1 && v <= 255)
		{
			dev_info_par.cap = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK CAP %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR CAP\n");
	}
	else if (strncasecmp(cmd, "INST ", 5) == 0)
	{
		int v = atoi(cmd + 5);
		if (v >= 0 && v <= 15)
		{
			dev_info_par.devInstance = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK INST %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR INST\n");
	}
	else
	{
		BLE_SendString("ERR ?\n");
	}
}
