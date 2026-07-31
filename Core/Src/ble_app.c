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
extern char hw_id_str[];			/* Laufzeit-Kennung "1003A" (main.c) */
extern volatile uint8_t error_mode;	/* ERROR_*-Bits (app_types.h) */
extern volatile int32_t raw_press;
extern volatile int32_t press_unfilt;	/* ungefilterter Messdruck (uBar, main.c) */
extern uint16_t wertung;				/* EMA-Filter: Altanteil in Promille (main.c) */
extern volatile uint8_t dev_info;
extern volatile uint8_t bonds_req;		/* BONDS-Diagnose anfragen (main.c) */

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
 * Format:  STAT;L=<%>;T=<C>;F=<typ>;C=<L>;I=<inst>;CAL=<0/1>;V=<x.y.z>;HWV=<variante><rev>;E=<fehler>;P=<uBar>\n
 * L: Füllstand in %, T: Temperatur in Grad C, F: Fluidtyp (0..15),
 * C: Kapazität (Liter), I: Instanz, CAL: 1 = kalibriert,
 * HWV: Hardware-Variante plus Platinen-Buchstabe, z. B. 1003A (die Zahl
 * ordnet die passende Firmware zu, der Buchstabe ist rein informativ).
 * Das fruehere Feld HW= (Revision) ist entfallen. Die Kennung kommt aus
 * dem OTP, wenn das Geraet provisioniert ist (hw_otp.h, Issue #2).
 * E: Fehlerbits (ERROR_* aus app_types.h), 0 = kein Fehler.
 * P: ungefilterter Messdruck in uBar (offsetkorrigiert, vor dem EMA-
 *    Filter) - fuer Rohwert-Anzeige und Kalibrier-Dialoge in der App.
 */
void ble_send_status(void)
{
	char line[112];

	/* percent_val: 100,00 % = 10000 */
	int p_int = percent_val / 100;
	int p_frac = (percent_val % 100) / 10;

	/* temp: 0,01 Grad C, Vorzeichen sauber behandeln */
	int16_t t = sensor_data_rx.temp;
	const char *tsign = (t < 0) ? "-" : "";
	int ta = (t < 0) ? -t : t;

	int cal = (EEPROM_values.calib_available == 0x00) ? 1 : 0;

	snprintf(line, sizeof(line), "STAT;L=%d.%d;T=%s%d.%02d;F=%d;C=%d;I=%d;CAL=%d;V=%s;HWV=%s;E=%u;P=%ld\n",
			 p_int, p_frac, tsign, ta / 100, ta % 100,
			 dev_info_par.fluidType, dev_info_par.cap, dev_info_par.devInstance, cal,
			 FW_VERSION, hw_id_str, (unsigned)error_mode, (long)press_unfilt);

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
 * Spezifikation in BLE_Protokoll.md. Unterstützt (case-insensitive):
 *   VER            Firmware-Version senden (VER;x.y.z)
 *   GET            aktuellen Status sofort senden (STAT;...)
 *   LIN            aktuelle Tankform-Kennlinie senden (LIN;...)
 *   LIN v0,...,v10 Kennlinie setzen (11 Werte 0..100, steigend)
 *   CAL100         aktuellen Druck als 100 % kalibrieren
 *   CAL0           aktuellen Druck als Nullpunkt uebernehmen (Tank leer!)
 *   CAL0RESET      nur den Nullpunkt auf Werkswert (100 % bleibt erhalten)
 *   CALRESET       100%-Kalibrierung auf Werkswert (Nullpunkt bleibt)
 *   FILT           Filterstaerke abfragen (FILT;<0..990>)
 *   FILT <0..990>  Filterstaerke setzen (Anteil alter Wert in Promille)
 *   PIN <6 Ziffern> Kopplungs-PIN aendern; loescht alle Bonds, Modul startet
 *                  neu, alle Geraete muessen sich neu koppeln
 *   BONDS          Diagnose: Anzahl Bonds im Modul + letzter Security-Status
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
		/* Ungefilterter Wert: die App zeigt press_unfilt als Rohdruck an -
		 * kalibriert wird genau das, was der Nutzer sieht. Der gefilterte
		 * raw_press hinkt bei hoher FILT-Einstellung um Sekunden hinterher. */
		if (press_unfilt >= 100)	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz) */
		{
			EEPROM_values.max_val = press_unfilt / 100;
			EEPROM_values.calib_available = 0x00;
			save_EEPROM(&EEPROM_values);
			BLE_SendString("OK CAL100\n");
		}
		else
		{
			BLE_SendString("ERR CAL100 nodruck\n");
		}
	}
	else if ((strncasecmp(cmd, "CAL0", 4) == 0) && (cmd[4] == '\0'))
	{
		/* Nullpunkt: aktuellen Druck als 0 uebernehmen - bei LEEREM Tank
		 * aufrufen. Bewusst der UNGEFILTERTE Wert (press_unfilt): den zeigt
		 * die App als Rohdruck an, und er reagiert sofort - der gefilterte
		 * raw_press hinkt bei hoher FILT-Einstellung um Sekunden hinterher.
		 * Die Sensortreiber ziehen den Offset direkt nach dem Einlesen ab;
		 * press_unfilt enthaelt also schon den alten Offset -> aufaddieren. */
		int32_t new_ofs = (int32_t)EEPROM_values.offset + press_unfilt;
		if ((new_ofs >= -30000) && (new_ofs <= 30000))	/* max. +/-30 mBar Drift */
		{
			EEPROM_values.offset = (int16_t)new_ofs;
			save_EEPROM(&EEPROM_values);
			raw_press = 0;	/* Filterzustand auf den neuen Nullpunkt setzen */
			snprintf(resp, sizeof(resp), "OK CAL0 %d\n", (int)EEPROM_values.offset);
			BLE_SendString(resp);
		}
		else
		{
			BLE_SendString("ERR CAL0 range\n");
		}
	}
	else if (strncasecmp(cmd, "CAL0RESET", 9) == 0)
	{
		/* Nur den Nullpunkt verwerfen, die 100%-Kalibrierung bleibt.
		 * Filterzustand auf den wieder unkorrigierten Messwert setzen,
		 * damit die Anzeige nicht mit grosser Zeitkonstante nachzieht. */
		raw_press = press_unfilt + EEPROM_values.offset;
		EEPROM_values.offset = std_offset;
		save_EEPROM(&EEPROM_values);
		BLE_SendString("OK CAL0RESET\n");
	}
	else if ((strncasecmp(cmd, "CAL", 3) == 0) && (cmd[3] == '\0'))
	{
		/* Abfrage der Kalibrierwerte fuer die Sicherung:
		 *   CAL;<0|1>;<max_val>;<offset>
		 * max_val ist der Rohdruck bei 100 % - genau der Wert, den CAL100
		 * aus raw_press bildet, offset ist der Nullpunkt aus CAL0. Damit
		 * laesst sich eine Kalibrierung spaeter ohne vollen Tank
		 * wiederherstellen. Die Pruefung auf das Stringende grenzt sauber
		 * gegen CAL0, CAL100 und CALRESET ab. */
		snprintf(resp, sizeof(resp), "CAL;%d;%lu;%d\n",
				 (EEPROM_values.calib_available == 0x00) ? 1 : 0,
				 (unsigned long)EEPROM_values.max_val,
				 (int)EEPROM_values.offset);
		BLE_SendString(resp);
	}
	else if (strncasecmp(cmd, "CAL ", 4) == 0)
	{
		/* Kalibrierwerte direkt setzen (Wiederherstellung aus einer Sicherung):
		 * CAL <max_val>[,<offset>]. Bewusst ohne Druckmessung - die Werte
		 * stammen aus dem Backup (Antwort der CAL-Abfrage). */
		long v = atol(cmd + 4);
		const char *sep = strchr(cmd + 4, ',');
		if (v >= 1 && v <= 1000000L)
		{
			EEPROM_values.max_val = (uint32_t)v;
			if (sep != NULL)
			{
				long o = atol(sep + 1);
				if ((o >= -30000) && (o <= 30000))
				{
					EEPROM_values.offset = (int16_t)o;
				}
			}
			EEPROM_values.calib_available = 0x00;
			save_EEPROM(&EEPROM_values);
			snprintf(resp, sizeof(resp), "OK CAL %lu\n",
					 (unsigned long)EEPROM_values.max_val);
			BLE_SendString(resp);
		}
		else
		{
			BLE_SendString("ERR CAL\n");
		}
	}
	else if (strncasecmp(cmd, "CALRESET", 8) == 0)
	{
		/* Nur die 100%-Kalibrierung verwerfen - der Nullpunkt (CAL0) bleibt
		 * stehen und hat mit CAL0RESET seinen eigenen Reset. */
		EEPROM_values.calib_available = 0xFF;
		EEPROM_values.max_val = std_press;
		save_EEPROM(&EEPROM_values);
		BLE_SendString("OK CALRESET\n");
	}
	else if ((strncasecmp(cmd, "FILT", 4) == 0) && (cmd[4] == '\0'))
	{
		snprintf(resp, sizeof(resp), "FILT;%u\n", (unsigned)wertung);
		BLE_SendString(resp);
	}
	else if (strncasecmp(cmd, "FILT ", 5) == 0)
	{
		/* EMA-Filterstaerke: Anteil des alten Werts in Promille. 0 = aus,
		 * 900 = Zeitkonstante ~1 s bei 100 ms Messtakt. Wirkt sofort und
		 * wird im Config persistiert. */
		int v = atoi(cmd + 5);
		if ((v >= 0) && (v <= 990))
		{
			wertung = (uint16_t)v;
			set_filt_eeprom(wertung);
			snprintf(resp, sizeof(resp), "OK FILT %d\n", v);
			BLE_SendString(resp);
		}
		else
		{
			BLE_SendString("ERR FILT\n");
		}
	}
	else if (strncasecmp(cmd, "PIN ", 4) == 0)
	{
		/* Kopplungs-PIN (Static Passkey) aendern: genau 6 Ziffern. Die
		 * Provisionierungs-Kette schreibt die PIN ins Modul, loescht alle
		 * Bonds (alte Kopplungen kaemen sonst ohne neue PIN weiter hinein)
		 * und startet das Modul neu - die Verbindung trennt sich dabei. */
		const char *pp = cmd + 4;
		uint8_t ok = (strlen(pp) == BLE_PIN_LEN);
		for (int i = 0; ok && (i < BLE_PIN_LEN); i++)
		{
			if ((pp[i] < '0') || (pp[i] > '9'))
			{
				ok = 0;
			}
		}
		if (ok)
		{
			/* PIN speichern (loescht den SECPROV-Marker) und den SENSOR neu
			 * starten: beim Boot laeuft die Provisionierungs-Kette, BEVOR ein
			 * Handy die Verbindung halten kann - im laufenden Betrieb verlor
			 * sie das Rennen gegen den Auto-Reconnect und brach mittendrin
			 * ab (Modul-Bonds blieben stehen -> Kopplungs-Sackgasse). */
			set_pin_eeprom(pp);
			BLE_SendString("OK PIN\n");
			HAL_Delay(300);		/* Antwort noch ueber die Luft lassen */
			__disable_irq();
			NVIC_SystemReset();
		}
		else
		{
			BLE_SendString("ERR PIN\n");
		}
	}
	else if ((strncasecmp(cmd, "BONDS", 5) == 0) && (cmd[5] == '\0'))
	{
		bonds_req = 1;	/* Antwort (BONDS;<n>;SEC=<s>) kommt asynchron */
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
