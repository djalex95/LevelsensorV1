/*
 * app_config.c
 *
 * Konfigurations-/EEPROM-Helfer (siehe app_config.h). Aus main.c
 * herausgezogen; Verhalten unveraendert. Arbeitet auf dem RAM-Cache
 * cfg_data[] (config_store.c) und schreibt Aenderungen ueber config_save().
 */
#include "app_config.h"
#include "config_store.h"
#include <string.h>

uint8_t check_EEPROM(void)
{
	if (cfg_data[0] == 0xFF){
		return 0;
	}
	else{
		return 1;
	}
}

void get_EEPROM(calib_data *values)
{
	values->calib_available = cfg_data[0];
	values->max_val = ((uint32_t)cfg_data[4]<<24)|((uint32_t)cfg_data[3]<<16)|((uint32_t)cfg_data[2]<<8)|(cfg_data[1]);
	values->offset = std_offset;
}

void save_EEPROM(calib_data *values)
{
	/* Nur die Kalibrierbytes im RAM-Cache aendern; config_save() schreibt
	 * den kompletten Block atomar (Ping-Pong, CRC). Bei Fehlschlag bleibt
	 * der alte Datensatz im Flash gueltig - kein Error_Handler noetig. */
	cfg_data[0] = values->calib_available;
	cfg_data[1] = (uint8_t)(values->max_val & 0xFF);
	cfg_data[2] = (uint8_t)((values->max_val >> 8) & 0xFF);
	cfg_data[3] = (uint8_t)((values->max_val >> 16) & 0xFF);
	cfg_data[4] = (uint8_t)((values->max_val >> 24) & 0xFF);

	config_save();
}


uint8_t check_dac_EEPROM(void)
{
	return cfg_data[8];
}

void get_dac_EEPROM(dac_calib_data *values)
{
	values->calib_available = cfg_data[8];
	values->dac_mx = ((uint32_t)cfg_data[12]<<24)|((uint32_t)cfg_data[11]<<16)|((uint32_t)cfg_data[10]<<8)|(cfg_data[9]);
	values->dac_c = ((uint32_t)cfg_data[16]<<24)|((uint32_t)cfg_data[15]<<16)|((uint32_t)cfg_data[14]<<8)|(cfg_data[13]);
}

void set_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values)
{
	cfg_data[17] = nmea_param->fluidType;
	cfg_data[18] = nmea_param->cap;
	memcpy(&cfg_data[19], values->lin_point, 11);
	cfg_data[31] = nmea_param->devInstance;		/* Byte 30 (Adresse) bleibt erhalten */

	config_save();
}

void get_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values)
{
	uint8_t lin_std[11] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

	if(cfg_data[17] == 0xFF)
	{
		nmea_param->fluidType = 0x01;
		nmea_param->cap = 100;
		nmea_param->devInstance = 0;
		memcpy(values->lin_point, &lin_std,11);
		set_param_eeprom(nmea_param, values);
	}
	else
	{
		nmea_param->fluidType = cfg_data[17];
		nmea_param->cap = cfg_data[18];
		memcpy(values->lin_point, &cfg_data[19], 11);
		nmea_param->devInstance = (cfg_data[31] <= 0x0F) ? cfg_data[31] : 0;

		if (!lin_table_valid(values->lin_point))	/* defekte Tabelle -> Identitaet */
		{
			memcpy(values->lin_point, &lin_std, 11);
		}
	}
}

/*
 * Prueft die Stuetzstellen-Tabelle: 11 Werte in Prozent (0..100),
 * monoton nicht fallend. lin_point[i] = Volumen-% bei Fuellhoehe i*10 %.
 */
uint8_t lin_table_valid(uint8_t *t)
{
	for (uint8_t i = 0; i < 11; i++)
	{
		if (t[i] > 100)
		{
			return 0;
		}
	}
	for (uint8_t i = 0; i < 10; i++)
	{
		if (t[i+1] < t[i])
		{
			return 0;
		}
	}
	return 1;
}

/* Config-Byte 30: zuletzt erfolgreich geclaimte NMEA2000-Quelladresse */
uint8_t get_adr_eeprom(void)
{
	uint8_t adr = cfg_data[30];

	if (adr > 251)		/* 0xFF = leer, >251 = ungueltig */
	{
		adr = 0x21;		/* Standardadresse */
	}
	return adr;
}

void set_adr_eeprom(uint8_t adr)
{
	if (cfg_data[30] == adr)
	{
		return;			/* nur bei Aenderung schreiben -> minimaler Flash-Verschleiss */
	}

	cfg_data[30] = adr;
	config_save();
}

/* Sensorname aus dem Config lesen (Bytes 33..56): 0x00-terminiert,
 * 0xFF = nie gesetzt -> leerer String. buf braucht CFG_NAME_LEN+1 Bytes. */
void get_name_eeprom(char *buf)
{
	uint8_t i;
	for (i = 0; i < CFG_NAME_LEN; i++)
	{
		uint8_t c = cfg_data[CFG_NAME_OFF + i];
		if ((c == 0x00) || (c == 0xFF))
		{
			break;
		}
		buf[i] = (char)c;
	}
	buf[i] = '\0';
}

/* Sensorname persistent speichern (auf CFG_NAME_LEN gekuerzt). */
void set_name_eeprom(const char *name)
{
	uint8_t i;
	for (i = 0; (i < CFG_NAME_LEN) && (name[i] != '\0'); i++)
	{
		cfg_data[CFG_NAME_OFF + i] = (uint8_t)name[i];
	}
	for (; i < CFG_NAME_LEN; i++)
	{
		cfg_data[CFG_NAME_OFF + i] = 0x00;
	}
	config_save();
}
