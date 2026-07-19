/*
 * ble.c
 *
 * Proteus-e (Würth 2612011024000) UART-Treiber – siehe ble.h.
 *
 *  Created on: Jun 9, 2024
 *      Author: a_hae
 */
#include "ble.h"
#include "main.h"		/* Pin-Defines BLE_RESET/BLE_MODE/BLE_BUSY, HAL */
#include <string.h>

static UART_HandleTypeDef *ble_uart = NULL;
static uint8_t rx_byte;					/* 1-Byte-Empfangspuffer für IT */

/* --- Zustand (in ISR geschrieben) --- */
volatile uint8_t ble_connected = 0;
volatile uint8_t ble_channel_open = 0;
volatile uint8_t ble_data_ready = 0;
uint8_t          ble_data_buf[BLE_MAX_PAYLOAD];
volatile uint16_t ble_data_len = 0;

/* Ausgehandelte max. Nutzdaten pro Frame (MPS, aus CMD_CHANNELOPEN_RSP).
 * Startwert = garantiertes Minimum (19). */
static uint8_t ble_mps = 19;

/* Aufgeschobene Namensänderung (wird nach dem Trennen angewendet) */
volatile uint8_t ble_setname_pending = 0;
static char ble_pending_name[21];

/* Aufgeschobene PIN-Änderung (wird nach dem Trennen angewendet) */
volatile uint8_t ble_setpin_pending = 0;
static char ble_pending_pin[BLE_PIN_LEN + 1];

/* Ergebnis einer Einstellungs-Abfrage (BLE_RequestSetting) */
volatile uint8_t  ble_get_ready = 0;
uint8_t           ble_get_index = 0xFF;
uint8_t           ble_get_value[21];
volatile uint16_t ble_get_len = 0;

/* --- Frame-Parser-Zustand --- */
typedef enum { ST_STX = 0, ST_CMD, ST_LEN_L, ST_LEN_H, ST_PAYLOAD, ST_CS } parse_state;
static parse_state pstate = ST_STX;
static uint8_t  frame_cmd;
static uint16_t frame_len;
static uint16_t frame_pos;
static uint8_t  frame_payload[BLE_MAX_PAYLOAD];
static uint8_t  frame_cs;				/* laufender XOR über STX..letztes Payloadbyte */

/* ------------------------------------------------------------------ intern */

static uint8_t xor_cs(const uint8_t *d, uint16_t len)
{
	uint8_t cs = 0;
	for (uint16_t i = 0; i < len; i++)
	{
		cs ^= d[i];
	}
	return cs;
}

/* Ein vollständig empfangenes, geprüftes Frame verarbeiten (ISR-Kontext). */
static void ble_dispatch(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
	switch (cmd)
	{
	case CMD_CONNECT_IND:
		ble_connected = 1;
		ble_channel_open = 0;		/* Kanal erst nach CHANNELOPEN_RSP */
		break;

	case CMD_CHANNELOPEN_RSP:
		/* Payload = Status(1) + BTMAC(6) + MPS(1). MPS = max. Nutzdaten/Frame. */
		if (len >= 8 && payload[7] >= 19 && payload[7] <= BLE_MAX_PAYLOAD)
		{
			ble_mps = payload[7];
		}
		ble_channel_open = 1;		/* jetzt darf gesendet werden */
		break;

	case CMD_DISCONNECT_IND:
		ble_connected = 0;
		ble_channel_open = 0;
		break;

	case CMD_GET_CNF:
		/* Antwort auf CMD_GET_REQ: Status(1, 0x00 = ok) + Einstellungswert.
		 * Die CNF enthaelt den Settings-Index NICHT - er wird beim Request
		 * in ble_get_index gemerkt (immer nur eine Anfrage offen). */
		if (!ble_get_ready && (ble_get_index != 0xFF) && len >= 1
				&& payload[0] == 0x00)
		{
			uint16_t nl = len - 1;
			if (nl > 20)
			{
				nl = 20;
			}
			memcpy(ble_get_value, payload + 1, nl);
			ble_get_value[nl] = '\0';
			ble_get_len = nl;
			ble_get_ready = 1;
		}
		break;

	case CMD_DATA_IND:
		/* Payload = BTMAC(6) + RSSI(1) + Nutzdaten. Nur die Nutzdaten übernehmen,
		 * und nur wenn die Hauptschleife das letzte Paket abgeholt hat. */
		if (!ble_data_ready && len > 7 && (uint16_t)(len - 7) <= BLE_MAX_PAYLOAD)
		{
			memcpy(ble_data_buf, payload + 7, len - 7);
			ble_data_len = len - 7;
			ble_data_ready = 1;
		}
		break;

	default:
		/* CMD_DATA_CNF, CMD_TXCOMPLETE_RSP, CMD_SECURITY_IND, GETSTATE_CNF … ignorieren */
		break;
	}
}

/* ------------------------------------------------------------------ API */

void BLE_ProcessByte(uint8_t b)
{
	switch (pstate)
	{
	case ST_STX:
		if (b == BLE_STX)
		{
			frame_cs = b;
			pstate = ST_CMD;
		}
		break;

	case ST_CMD:
		frame_cmd = b;
		frame_cs ^= b;
		pstate = ST_LEN_L;
		break;

	case ST_LEN_L:
		frame_len = b;
		frame_cs ^= b;
		pstate = ST_LEN_H;
		break;

	case ST_LEN_H:
		frame_len |= (uint16_t)b << 8;
		frame_cs ^= b;
		frame_pos = 0;
		if (frame_len > BLE_MAX_PAYLOAD)
		{
			pstate = ST_STX;		/* unplausibel -> verwerfen */
		}
		else
		{
			pstate = (frame_len == 0) ? ST_CS : ST_PAYLOAD;
		}
		break;

	case ST_PAYLOAD:
		frame_payload[frame_pos++] = b;
		frame_cs ^= b;
		if (frame_pos >= frame_len)
		{
			pstate = ST_CS;
		}
		break;

	case ST_CS:
		if (b == frame_cs)			/* Prüfsumme ok */
		{
			ble_dispatch(frame_cmd, frame_payload, frame_len);
		}
		pstate = ST_STX;
		break;

	default:
		pstate = ST_STX;
		break;
	}
}

/*
 * Sendet Nutzdaten über den offenen Kanal (CMD_DATA_REQ). Längere Daten werden
 * in MPS-große Frames zerlegt. Der BUSY/UART_ENABLE-Pin wird NICHT abgefragt:
 * im Command mode ist er ein Modul-Eingang (Pull-up), kein Busy-Ausgang – die
 * UART ist per Default aktiv.
 */
uint8_t BLE_SendData(const uint8_t *data, uint16_t len)
{
	uint8_t frame[BLE_FRAME_MAX];
	uint16_t sent = 0;

	if (!ble_channel_open || ble_uart == NULL || len == 0)
	{
		return 0;
	}

	while (sent < len)
	{
		uint16_t chunk = len - sent;
		if (chunk > ble_mps)
		{
			chunk = ble_mps;
		}

		frame[0] = BLE_STX;
		frame[1] = CMD_DATA_REQ;
		frame[2] = (uint8_t)(chunk & 0xFF);
		frame[3] = (uint8_t)((chunk >> 8) & 0xFF);
		memcpy(&frame[4], data + sent, chunk);
		frame[4 + chunk] = xor_cs(frame, 4 + chunk);

		if (HAL_UART_Transmit(ble_uart, frame, 5 + chunk, 200) != HAL_OK)
		{
			return 0;
		}
		sent += chunk;

		if (sent < len)
		{
			HAL_Delay(5);	/* Modul kurz Zeit zum Verarbeiten geben */
		}
	}
	return 1;
}

uint8_t BLE_SendString(const char *s)
{
	return BLE_SendData((const uint8_t *)s, (uint16_t)strlen(s));
}

/* Sendet CMD_SET_REQ fuer eine Einstellung (nur im ACTION_IDLE erlaubt).
 * STX | 0x11 | Len(2) | SettingsIndex(1) | Wert | CS, Len = 1 + Wertlaenge. */
static void ble_send_set(uint8_t idx, const uint8_t *data, uint16_t n)
{
	uint8_t frame[BLE_FRAME_MAX];
	uint16_t len = 1 + n;

	frame[0] = BLE_STX;
	frame[1] = CMD_SET_REQ;
	frame[2] = (uint8_t)(len & 0xFF);
	frame[3] = (uint8_t)((len >> 8) & 0xFF);
	frame[4] = idx;
	memcpy(&frame[5], data, n);
	frame[5 + n] = xor_cs(frame, 5 + n);

	HAL_UART_Transmit(ble_uart, frame, 6 + n, 200);
}

/* Sendet ein Kommando ohne Payload (z. B. RESET, DISCONNECT, DELETEBONDS). */
static void ble_send_cmd0(uint8_t cmd)
{
	uint8_t frame[5];

	frame[0] = BLE_STX;
	frame[1] = cmd;
	frame[2] = 0;
	frame[3] = 0;
	frame[4] = xor_cs(frame, 4);
	HAL_UART_Transmit(ble_uart, frame, 5, 100);
}

/* CMD_SET_REQ zum Umbenennen; das Modul startet danach selbst neu. */
static void ble_send_setname(const char *name)
{
	uint16_t nl = (uint16_t)strlen(name);
	if (nl > 20) nl = 20;
	ble_send_set(CFG_IDX_DEVICENAME, (const uint8_t *)name, nl);
}

uint8_t BLE_SetDeviceName(const char *name)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	strncpy(ble_pending_name, name, 20);
	ble_pending_name[20] = '\0';

	if (ble_connected)
	{
		/* CMD_SET_REQ ist im verbundenen Zustand nicht erlaubt -> erst trennen.
		 * Die Hauptschleife wendet den Namen nach CMD_DISCONNECT_IND an. */
		ble_setname_pending = 1;
		ble_send_cmd0(CMD_DISCONNECT_REQ);
	}
	else
	{
		ble_send_setname(ble_pending_name);
	}
	return 1;
}

/* Fragt eine im Modul-Flash gespeicherte Einstellung ab (CMD_GET_REQ).
 * Lesen ist - anders als CMD_SET_REQ - jederzeit erlaubt. Die Antwort
 * (CMD_GET_CNF) verarbeitet ble_dispatch() asynchron. */
uint8_t BLE_RequestSetting(uint8_t idx)
{
	uint8_t frame[6];

	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_get_ready = 0;
	ble_get_index = idx;
	frame[0] = BLE_STX;
	frame[1] = CMD_GET_REQ;
	frame[2] = 1;					/* Len = 1: nur der Settings-Index */
	frame[3] = 0;
	frame[4] = idx;
	frame[5] = xor_cs(frame, 5);

	return (HAL_UART_Transmit(ble_uart, frame, 6, 100) == HAL_OK) ? 1 : 0;
}

/* Sicherheitsmodus setzen: RF_SecFlags schreiben, Bonds loeschen (laut
 * Manual bei SecFlags-Aenderung erforderlich) und Modul neu starten.
 * Nur im getrennten Zustand aufrufen. */
uint8_t BLE_SetSecFlags(uint8_t flags)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_send_set(CFG_IDX_SECFLAGS, &flags, 1);
	HAL_Delay(50);
	ble_send_cmd0(CMD_DELETEBONDS_REQ);
	HAL_Delay(50);
	ble_send_cmd0(CMD_RESET_REQ);	/* Einstellungen aktivieren */
	return 1;
}

uint8_t BLE_ClearBonds(void)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_send_cmd0(CMD_DELETEBONDS_REQ);
	HAL_Delay(50);
	ble_send_cmd0(CMD_RESET_REQ);	/* sauberer Neuanlauf, frisches Advertising */
	return 1;
}

void BLE_Disconnect(void)
{
	if (ble_uart != NULL)
	{
		ble_send_cmd0(CMD_DISCONNECT_REQ);
	}
}

uint8_t BLE_SetPin(const char *pin)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	memcpy(ble_pending_pin, pin, BLE_PIN_LEN);
	ble_pending_pin[BLE_PIN_LEN] = '\0';

	if (ble_connected)
	{
		/* wie beim Namen: erst trennen, die Hauptschleife wendet die PIN
		 * nach CMD_DISCONNECT_IND an */
		ble_setpin_pending = 1;
		ble_send_cmd0(CMD_DISCONNECT_REQ);
	}
	else
	{
		BLE_ApplyPendingPin();
	}
	return 1;
}

void BLE_ApplyPendingPin(void)
{
	ble_setpin_pending = 0;
	ble_send_set(CFG_IDX_STATICPASSKEY,
			(const uint8_t *)ble_pending_pin, BLE_PIN_LEN);
	HAL_Delay(50);
	/* Alte Kopplungen ungueltig machen - sonst kaemen bereits gebondete
	 * Geraete weiterhin ohne die neue PIN hinein. */
	ble_send_cmd0(CMD_DELETEBONDS_REQ);
	HAL_Delay(50);
	ble_send_cmd0(CMD_RESET_REQ);	/* neue PIN aktivieren */
}

void BLE_ApplyPendingName(void)
{
	ble_setname_pending = 0;
	ble_send_setname(ble_pending_name);
}

void BLE_Init(UART_HandleTypeDef *huart)
{
	ble_uart = huart;
	pstate = ST_STX;
	ble_connected = 0;
	ble_channel_open = 0;
	ble_data_ready = 0;

	/* MODE low = Kommando-Modus (Standard); Reset-Sequenz (aktiv low). */
	HAL_GPIO_WritePin(BLE_MODE_GPIO_Port, BLE_MODE_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(10);

	/* Byteweisen Empfang starten, dann Reset lösen – so gehen keine
	 * Startup-Meldungen des Moduls verloren. */
	HAL_UART_Receive_IT(ble_uart, &rx_byte, 1);
	HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_SET);
	/* Modul bootet und beginnt selbstständig zu advertisen (~ einige 100 ms). */
}

/* UART-Empfangs-Callback: Byte in den Parser geben und neu scharf schalten. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == ble_uart->Instance)
	{
		BLE_ProcessByte(rx_byte);
		HAL_UART_Receive_IT(huart, &rx_byte, 1);
	}
}
