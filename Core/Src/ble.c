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

/* Je Verbindung genau einmal protokollieren: der erste unterdrueckte und der
 * erste tatsaechlich abgeschickte Sendeversuch. Nutzdaten laufen sonst nicht
 * ins Protokoll (sie wuerden den Ringpuffer in Sekunden ueberschreiben),
 * genau diese beiden Ereignisse sind aber die interessanten. */
static uint8_t ble_tx_log_blocked = 0;
static uint8_t ble_tx_log_sent = 0;

/* Aufgeschobene Namensänderung (wird nach dem Trennen angewendet) */
volatile uint8_t ble_setname_pending = 0;
static char ble_pending_name[21];

/* Ergebnis einer Einstellungs-Abfrage (BLE_RequestSetting) */
volatile uint8_t  ble_get_ready = 0;
uint8_t           ble_get_index = 0xFF;
uint8_t           ble_get_value[21];
volatile uint16_t ble_get_len = 0;

/* Bestaetigungen fuer die Provisionierungs-Kette (siehe ble.h) */
volatile uint8_t ble_set_cnf = 0;
volatile uint8_t ble_delbonds_cnf = 0;
volatile uint8_t ble_boot_seen = 0;
volatile uint8_t ble_bonds_ready = 0;
volatile uint8_t ble_bonds_count = 0;
volatile uint8_t ble_bonds_status = 0xFF;
volatile uint8_t ble_sec_state = 0xFF;

/* --- Diagnose: Ereignis-Ringpuffer und Zustandsbild (siehe ble.h) --- */
volatile ble_log_entry ble_log[BLE_LOG_N];
volatile uint8_t ble_log_wr = 0;
volatile uint8_t ble_log_cnt = 0;
volatile ble_evt_entry ble_evt[BLE_EVT_N];
volatile uint8_t ble_evt_wr = 0;
volatile uint8_t ble_evt_cnt = 0;
ble_stat_t ble_stat;
ble_diag_t ble_diag;

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

/*
 * Ein Ereignis protokollieren. Wird sowohl aus der ISR (empfangene Frames)
 * als auch aus der Hauptschleife (gesendete Kommandos) aufgerufen - deshalb
 * kurz die Interrupts sperren und PRIMASK sauber wiederherstellen, damit ein
 * Aufruf aus bereits gesperrtem Kontext nichts kaputt macht.
 */
void BLE_LogAdd(uint8_t ev, uint8_t p)
{
	uint32_t pm = __get_PRIMASK();
	__disable_irq();

	ble_log[ble_log_wr].t  = (uint16_t)(HAL_GetTick() / 100U);
	ble_log[ble_log_wr].ev = ev;
	ble_log[ble_log_wr].p  = p;
	ble_log_wr = (uint8_t)((ble_log_wr + 1U) % BLE_LOG_N);
	if (ble_log_cnt < BLE_LOG_N)
	{
		ble_log_cnt++;
	}

	__set_PRIMASK(pm);
}

/*
 * Ein seltenes Ereignis ins Langzeit-Protokoll schreiben. Gleiche
 * Absicherung wie BLE_LogAdd - der Aufruf kommt aus beiden Kontexten.
 */
void BLE_EvtAdd(uint8_t ev, uint8_t p)
{
	uint32_t pm = __get_PRIMASK();
	__disable_irq();

	ble_evt[ble_evt_wr].t  = HAL_GetTick() / 1000U;
	ble_evt[ble_evt_wr].ev = ev;
	ble_evt[ble_evt_wr].p  = p;
	ble_evt_wr = (uint8_t)((ble_evt_wr + 1U) % BLE_EVT_N);
	if (ble_evt_cnt < BLE_EVT_N)
	{
		ble_evt_cnt++;
	}

	__set_PRIMASK(pm);
}

/* Ein vollständig empfangenes, geprüftes Frame verarbeiten (ISR-Kontext). */
static void ble_dispatch(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
	/* Nutzdatenverkehr nicht mitschreiben - er wuerde den kleinen Ringpuffer
	 * in Sekunden ueberschreiben und sagt ueber eine gescheiterte Kopplung
	 * nichts aus. Alles andere kommt hinein, insbesondere CMD_DISCONNECT_IND
	 * (mit Grund) und CMD_GETSTATE_CNF (= das Modul ist neu gestartet). */
	if ((cmd != CMD_DATA_IND) && (cmd != CMD_DATA_CNF)
			&& (cmd != CMD_TXCOMPLETE_RSP))
	{
		BLE_LogAdd(cmd, (len >= 1) ? payload[0] : 0);
	}

	/* Zusaetzlich ins Langzeit-Protokoll, aber nur das Seltene: ein
	 * Modul-Neustart, ein Pairing, das nicht auf einen bekannten Bond
	 * trifft, und Fehlermeldungen des Moduls. Alles andere wuerde die
	 * 24 Plaetze in Stunden fuellen und damit nutzlos machen. */
	switch (cmd)
	{
	case CMD_GETSTATE_CNF:
		ble_stat.modboot++;
		BLE_EvtAdd(BLE_EVT_MODBOOT, (len >= 1) ? payload[0] : 0);
		break;
	case CMD_SECURITY_IND:
		/* 0x00 = mit bekanntem Bond wiederverbunden, der Normalfall.
		 * Alles andere heisst: es wurde neu gekoppelt oder es ging
		 * schief - genau die Momente, um die es hier geht. */
		if ((len >= 1) && (payload[0] != 0x00))
		{
			BLE_EvtAdd(BLE_EVT_SEC, payload[0]);
		}
		break;
	case 0xA2:	/* CMD_ERROR_IND des Proteus-e */
		BLE_EvtAdd(BLE_EVT_MODERR, (len >= 1) ? payload[0] : 0);
		break;
	default:
		break;
	}

	switch (cmd)
	{
	case CMD_CONNECT_IND:
		ble_connected = 1;
		ble_channel_open = 0;		/* Kanal erst nach CHANNELOPEN_RSP */
		ble_sec_state = 0xFF;		/* Sicherheitszustand dieser Verbindung */
		ble_tx_log_blocked = 1;
		ble_tx_log_sent = 1;
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

	case CMD_GETSTATE_CNF:
		/* Kommt nach jedem Modul-(Neu)start spontan - dient der
		 * Provisionierungs-Kette als "Modul ist wieder da"-Signal. */
		ble_boot_seen = 1;
		break;

	case CMD_SET_CNF:
		ble_set_cnf = (len >= 1 && payload[0] == 0x00) ? 1 : 2;
		break;

	case CMD_DELETEBONDS_CNF:
		ble_delbonds_cnf = (len >= 1 && payload[0] == 0x00) ? 1 : 2;
		break;

	case CMD_GETBONDS_CNF:
		/* Status(1) + Anzahl(1) + je Bond: ID(2) + BTMAC(6) */
		ble_bonds_status = (len >= 1) ? payload[0] : 0xFE;
		if (len >= 2 && payload[0] == 0x00)
		{
			ble_bonds_count = payload[1];
			ble_bonds_ready = 1;
		}
		else
		{
			ble_bonds_ready = 2;
		}
		break;

	case CMD_SECURITY_IND:
		/* Status(1) + BTMAC(6): 0x00 = re-bonded (bekannter Partner),
		 * 0x01 = frisch gebondet. Fuer die BONDS-Diagnose gemerkt. */
		if (len >= 1)
		{
			ble_sec_state = payload[0];
		}
		break;

	default:
		/* CMD_DATA_CNF, CMD_TXCOMPLETE_RSP … ignorieren */
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

	if (ble_uart == NULL || len == 0)
	{
		return 0;
	}

	/* Nur ueber einen offenen UND verschluesselten Kanal senden. Der Kanal
	 * geht schon auf, waehrend die Kopplung noch laeuft; wer dort hinein
	 * sendet, laesst das Modul (FW 1.0.0) mit CMD_ERROR_IND neu starten und
	 * bringt damit jede Kopplung um. ble_sec_state != 0xFF heisst: das Modul
	 * hat CMD_SECURITY_IND gemeldet, die Verbindung ist verschluesselt. */
	if (!ble_channel_open || (ble_sec_state == 0xFF))
	{
		if (ble_tx_log_blocked)
		{
			ble_tx_log_blocked = 0;
			BLE_LogAdd(BLE_LOG_EV_TXBLOCK, ble_channel_open);
		}
		return 0;
	}

	if (ble_tx_log_sent)
	{
		ble_tx_log_sent = 0;
		BLE_LogAdd((uint8_t)(0x20U | CMD_DATA_REQ), (uint8_t)len);
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
	BLE_LogAdd((uint8_t)(0x20U | CMD_SET_REQ), idx);
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
	BLE_LogAdd((uint8_t)(0x20U | cmd), 0);
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

	BLE_LogAdd((uint8_t)(0x20U | CMD_GET_REQ), idx);
	return (HAL_UART_Transmit(ble_uart, frame, 6, 100) == HAL_OK) ? 1 : 0;
}

/* --- Bausteine der Sicherheits-Provisionierung ---
 * Nicht blockierend: jede Funktion schickt genau ein Kommando und loescht
 * vorher die zugehoerigen Bestaetigungs-Flags. Der Proteus-e startet nach
 * jedem CMD_SET_REQ SELBST neu - der Aufrufer (Schritt-Kette in main.c)
 * wartet deshalb nach ble_set_cnf zusaetzlich auf ble_boot_seen, bevor er
 * das naechste Kommando schickt. */

uint8_t BLE_SetSecFlags(uint8_t flags)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_set_cnf = 0;
	ble_boot_seen = 0;
	ble_send_set(CFG_IDX_SECFLAGS, &flags, 1);
	return 1;
}

uint8_t BLE_SetPasskey(const char *pin6)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_set_cnf = 0;
	ble_boot_seen = 0;
	ble_send_set(CFG_IDX_STATICPASSKEY, (const uint8_t *)pin6, BLE_PIN_LEN);
	return 1;
}

uint8_t BLE_DeleteBonds(void)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_delbonds_cnf = 0;
	BLE_EvtAdd(BLE_EVT_DELBONDS, 0);
	ble_send_cmd0(CMD_DELETEBONDS_REQ);
	return 1;
}

uint8_t BLE_ResetModule(void)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_boot_seen = 0;
	BLE_EvtAdd(BLE_EVT_MODRESET, 0);
	ble_send_cmd0(CMD_RESET_REQ);
	/* Ein Reset trennt die Funkverbindung, das Modul sendet dabei KEIN
	 * CMD_DISCONNECT_IND -> Zustand hier selbst zuruecksetzen. */
	ble_connected = 0;
	ble_channel_open = 0;
	return 1;
}

uint8_t BLE_RequestBonds(void)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_bonds_ready = 0;
	ble_send_cmd0(CMD_GETBONDS_REQ);
	return 1;
}

/* Zustandsabfrage. Wird von der Provisionierungs-Kette benutzt, um einen
 * erwarteten Modul-Neustart selbst festzustellen: waehrend das Modul bootet
 * antwortet es nicht, danach schickt es CMD_GETSTATE_CNF - und das ist
 * dieselbe Meldung wie die spontane Boot-Meldung, setzt also ble_boot_seen.
 * Erst nach der Bootzeit fragen, sonst antwortet noch die alte Instanz. */
uint8_t BLE_RequestState(void)
{
	if (ble_uart == NULL)
	{
		return 0;
	}
	ble_send_cmd0(CMD_GETSTATE_REQ);
	return 1;
}

void BLE_Disconnect(void)
{
	if (ble_uart != NULL)
	{
		ble_send_cmd0(CMD_DISCONNECT_REQ);
	}
}

void BLE_ApplyPendingName(void)
{
	ble_setname_pending = 0;
	ble_send_setname(ble_pending_name);
}

void BLE_Init(UART_HandleTypeDef *huart)
{
	/* Erster Eintrag nach jedem STM32-Start. Steht er mitten im Protokoll,
	 * hat sich der STM32 neu gestartet - und hat dabei ueber MX_GPIO_Init
	 * auch die Resetleitung des Moduls gezogen. */
	BLE_LogAdd(BLE_LOG_EV_MCUBOOT, 0);
	BLE_EvtAdd(BLE_EVT_MCUBOOT, 0);

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
