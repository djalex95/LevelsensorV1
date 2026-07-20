/*
 * nmea_app.c
 *
 * NMEA2000-Handler der Applikationsebene (siehe nmea_app.h).
 * Aus main.c herausgezogen; Verhalten unveraendert.
 */
#include "main.h"
#include "nmea_app.h"
#include "ble_app.h"
#include "app_types.h"
#include "app_config.h"
#include "config_store.h"
#include <string.h>

/* Proprietary-Header fuer PGN 126720: MFR-Code 2046, reserved 0x3, Industry
 * Group 4 -> uint16 LE 0x9FFE -> Bytes 0xFE, 0x9F */
#define PROP_HDR_0 0xFE
#define PROP_HDR_1 0x9F
#define PROP_CMD_SET_LIN 0x01
#define PROP_CMD_GET_LIN 0x02
#define PROP_CMD_CALIB   0x03	/* aktuellen Druck als 100 % (max_val) kalibrieren */
#define PROP_CMD_RESET   0x04	/* Kalibrierung auf Werkswert zuruecksetzen */
#define PROP_CMD_FRESET  0x05	/* Werksreset: kompletten Config loeschen + Neustart */

/* CubeMX-Handle und App-Zustand - definiert in main.c.
 * (gf_buf/gf_len/gf_src kommen aus nmea2000.h.) */
extern FDCAN_HandleTypeDef hfdcan1;
extern NMEA_parameter_Device dev_info_par;
extern prod_param device_param;
extern calib_data EEPROM_values;
extern char sensor_name[CFG_NAME_LEN + 1];
extern uint16_t percent_val;
extern volatile int32_t raw_press;
extern uint32_t claim_time;
extern volatile uint8_t dev_info;

/*
 * Verarbeitet eine komplett empfangene Group Function (PGN 126208).
 * Unterstuetzt fuer PGN 127505 (Fluid Level):
 *   Funktionscode 0 (Request): PGN sofort senden
 *   Funktionscode 1 (Command): Feld 1 = Instanz (0..15), Feld 2 = Fluidtyp (0..15),
 *                              Feld 4 = Kapazitaet (uint32, 0,1-L-Schritte)
 * Antwortet mit Acknowledge (Funktionscode 2) inkl. Fehlercodes je Parameter.
 * Hinweis: Feldwerte werden byte-aligned erwartet (wie in gaengigen
 * Open-Source-Stacks), nicht bit-gepackt.
 */
void handle_group_function(void)
{
	uint8_t fn = gf_buf[0];
	uint32_t target_pgn = gf_buf[1] | ((uint32_t)gf_buf[2] << 8) | ((uint32_t)gf_buf[3] << 16);

	if (target_pgn == 126998)
	{
		/* Configuration Information: Feld 1 = Installation Description 1
		 * (variabler String [Laenge n+2][0x01][n Zeichen]) -> Sensorname.
		 * So koennen auch Plotter den Namen setzen. */
		if (fn == 0)
		{
			dev_info++;		/* Request: 126998 senden */
		}
		else if (fn == 1)
		{
			uint8_t n = gf_buf[5];
			uint8_t pos = 6;
			uint8_t perr[8] = {0};
			uint8_t changed = 0;

			for (uint8_t i = 0; (i < n) && (i < 8) && (pos < gf_len); i++)
			{
				uint8_t field = gf_buf[pos++];
				uint8_t sl = (pos < gf_len) ? gf_buf[pos] : 0;

				if ((field == 1) && (sl >= 2) && ((uint16_t)(pos + sl) <= gf_len))
				{
					uint8_t cn = (uint8_t)(sl - 2);
					char nm[CFG_NAME_LEN + 1];
					if (cn > CFG_NAME_LEN)
					{
						cn = CFG_NAME_LEN;	/* zu lang -> kuerzen */
					}
					memcpy(nm, &gf_buf[pos + 2], cn);
					nm[cn] = '\0';
					if (strcmp(nm, sensor_name) != 0)
					{
						set_name_eeprom(nm);
						get_name_eeprom(sensor_name);
						/* BLE-Modulname sofort mitziehen (trennt ggf. eine
						 * bestehende BLE-Verbindung, Modul startet neu) */
						{
							char want[21];
							ble_desired_name(want);
							BLE_SetDeviceName(want);
						}
						changed = 1;
					}
					pos = (uint8_t)(pos + sl);
				}
				else
				{
					perr[i] = 4;	/* Feld nicht unterstuetzt / defekt */
					pos = gf_len;	/* Groesse unbekannt -> Abbruch */
				}
			}

			if (changed)
			{
				dev_info++;		/* aktualisierte Info gleich verschicken */
			}
			NMEA2000_SendGFAck(&hfdcan1, dev_info_par.srcAdr, gf_src, target_pgn, 0, perr, n);
		}
		return;
	}

	if (target_pgn != 127505)
	{
		if (fn == 1)	/* Kommandos auf fremde PGNs negativ quittieren */
		{
			NMEA2000_SendGFAck(&hfdcan1, dev_info_par.srcAdr, gf_src, target_pgn, 1, NULL, 0);
		}
		return;
	}

	if (fn == 0)		/* Request: einmal sofort senden */
	{
		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, percent_val, dev_info_par.cap);
	}
	else if (fn == 1)	/* Command */
	{
		uint8_t n = gf_buf[5];
		uint8_t pos = 6;
		uint8_t perr[8] = {0};
		uint8_t changed = 0;
		uint8_t inst_changed = 0;

		for (uint8_t i = 0; (i < n) && (i < 8) && (pos < gf_len); i++)
		{
			uint8_t field = gf_buf[pos++];
			switch (field)
			{
			case 1:		/* Instanz (4 bit im PGN) */
				if (gf_buf[pos] <= 0x0F)
				{
					if (dev_info_par.devInstance != gf_buf[pos])
					{
						dev_info_par.devInstance = gf_buf[pos];
						inst_changed = 1;
						changed = 1;
					}
				}
				else
				{
					perr[i] = 3;	/* ausserhalb Bereich */
				}
				pos += 1;
				break;

			case 2:		/* Fluidtyp (4 bit im PGN) */
				if (gf_buf[pos] <= 0x0F)
				{
					dev_info_par.fluidType = gf_buf[pos];
					changed = 1;
				}
				else
				{
					perr[i] = 3;
				}
				pos += 1;
				break;

			case 4:		/* Kapazitaet, uint32 in 0,1 L */
			{
				uint32_t cap01 = gf_buf[pos] | ((uint32_t)gf_buf[pos+1] << 8) | ((uint32_t)gf_buf[pos+2] << 16) | ((uint32_t)gf_buf[pos+3] << 24);
				uint32_t cap_l = cap01 / 10;
				if (cap_l <= 255)	/* EEPROM-Feld ist 1 Byte (Liter) */
				{
					dev_info_par.cap = (uint8_t)cap_l;
					changed = 1;
				}
				else
				{
					perr[i] = 3;
				}
				pos += 4;
				break;
			}

			default:	/* Feld 3 (Fuellstand) ist Messwert, Rest unbekannt */
				perr[i] = 4;	/* Parameter nicht unterstuetzt */
				pos = gf_len;	/* Feldgroesse unbekannt -> Abbruch */
				break;
			}
		}

		if (changed)
		{
			set_param_eeprom(&dev_info_par, &device_param);
		}
		if (inst_changed)
		{
			/* Instanz ist Teil des NAME -> Address Claim wiederholen */
			NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
			claim_time = HAL_GetTick();
		}

		NMEA2000_SendGFAck(&hfdcan1, dev_info_par.srcAdr, gf_src, target_pgn, 0, perr, n);
	}
}

/*
 * Verarbeitet proprietaere Konfiguration (PGN 126720):
 *   [Header 0xFE 0x9F] [0x01] [11 Stuetzstellen]  -> Tabelle schreiben, Antwort 0x81 + Status
 *   [Header 0xFE 0x9F] [0x02]                     -> Tabelle lesen,     Antwort 0x82 + 11 Werte
 */
void handle_prop_config(void)
{
	uint8_t reply[16];

	if ((gf_len < 3) || (gf_buf[0] != PROP_HDR_0) || (gf_buf[1] != PROP_HDR_1))
	{
		return;		/* nicht unser Herstellercode */
	}

	reply[0] = PROP_HDR_0;
	reply[1] = PROP_HDR_1;

	if ((gf_buf[2] == PROP_CMD_SET_LIN) && (gf_len >= 14))
	{
		uint8_t ok = lin_table_valid(&gf_buf[3]);
		if (ok)
		{
			memcpy(device_param.lin_point, &gf_buf[3], 11);
			set_param_eeprom(&dev_info_par, &device_param);
		}
		reply[2] = 0x81;
		reply[3] = ok ? 0 : 3;	/* 0 = OK, 3 = ungueltig (>100 oder nicht monoton) */
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
	else if (gf_buf[2] == PROP_CMD_GET_LIN)
	{
		reply[2] = 0x82;
		memcpy(&reply[3], device_param.lin_point, 11);
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 14);
	}
	else if (gf_buf[2] == PROP_CMD_CALIB)
	{
		/* Wie der Taster-Kalibriermodus: aktuellen Druck als 100 % setzen.
		 * Nur bei positivem Druck (Sensor plausibel angeschlossen). */
		uint8_t ok = 0;
		if (raw_press >= 100)	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz) */
		{
			EEPROM_values.max_val = raw_press / 100;
			EEPROM_values.calib_available = 0x00;
			save_EEPROM(&EEPROM_values);
			ok = 1;
		}
		reply[2] = 0x83;
		reply[3] = ok ? 0 : 1;	/* 1 = kein gueltiger Druck */
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
	else if (gf_buf[2] == PROP_CMD_RESET)
	{
		/* Kalibrierung verwerfen -> Werkswert (std_press) beim naechsten Boot */
		EEPROM_values.calib_available = 0xFF;
		save_EEPROM(&EEPROM_values);
		EEPROM_values.max_val = std_press;
		reply[2] = 0x84;
		reply[3] = 0;
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
	else if (gf_buf[2] == PROP_CMD_FRESET)
	{
		/* Werksreset: erst bestaetigen (damit das PC-Tool die Antwort sieht),
		 * dann kompletten Config-Speicher loeschen und neu starten. Nach dem
		 * Boot gelten Werkswerte: Adresse 0x21, unkalibriert, kein Name. */
		reply[2] = 0x85;
		reply[3] = 0;
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
		HAL_Delay(100);
		config_factory_reset();
		__disable_irq();
		NVIC_SystemReset();
	}
}

/*
 * PGN 65240 Commanded Address (ISO 11783-5): 8 Byte NAME (LE) + 1 Byte neue
 * Quelladresse. Gilt nur, wenn der NAME exakt unserer ist - so kann ein
 * Bus-Tool gezielt einem Geraet eine Adresse zuweisen.
 */
void handle_commanded_address(void)
{
	uint64_t name = 0;

	if (gf_len < 9)
	{
		return;
	}
	for (int8_t i = 7; i >= 0; i--)
	{
		name = (name << 8) | gf_buf[i];
	}
	if (name != NMEA2000_BuildOwnName())
	{
		return;			/* anderes Geraet gemeint */
	}
	if (gf_buf[8] > 251)
	{
		return;			/* 252..255 sind keine gueltigen Quelladressen */
	}

	dev_info_par.srcAdr = gf_buf[8];
	NMEA2000_change_address(&hfdcan1, dev_info_par.srcAdr);
	NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
	claim_time = HAL_GetTick();
	set_adr_eeprom(dev_info_par.srcAdr);
}
