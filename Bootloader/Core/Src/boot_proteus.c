/*
 * boot_proteus.c  – siehe boot_proteus.h
 *
 * Spiegelt die Frame-Verarbeitung aus der App (ble.c), aber ohne Interrupts:
 * BP_Poll() liest die UART per Polling. Das DFU-Protokoll ist Lock-Step
 * (Handy wartet auf jede Antwort), daher droht während Flash-Operationen kein
 * UART-Überlauf.
 */

#include "boot_proteus.h"
#include "main.h"        /* Pin-Defines BLE_RESET/BLE_MODE/BLE_BUSY */
#include <string.h>

/* --- Proteus-Kommandos (siehe Referenzhandbuch) --- */
#define BLE_STX               0x02
#define CMD_DATA_REQ          0x04
#define CMD_DATA_IND          0x84
#define CMD_CONNECT_IND       0x86
#define CMD_DISCONNECT_IND    0x87
#define CMD_CHANNELOPEN_RSP   0xC6
#define BP_FRAME_MAX          (BP_MAX_PAYLOAD + 5)

static UART_HandleTypeDef *bp_uart;

volatile uint8_t bp_connected = 0;
volatile uint8_t bp_channel_open = 0;
volatile uint8_t bp_data_ready = 0;
uint8_t          bp_data_buf[BP_MAX_PAYLOAD];
volatile uint16_t bp_data_len = 0;
static uint8_t   bp_mps = 19;

/* Frame-Parser-Zustand */
typedef enum { ST_STX = 0, ST_CMD, ST_LEN_L, ST_LEN_H, ST_PAYLOAD, ST_CS } pstate_t;
static pstate_t pstate = ST_STX;
static uint8_t  f_cmd;
static uint16_t f_len, f_pos;
static uint8_t  f_payload[BP_MAX_PAYLOAD];
static uint8_t  f_cs;

static uint8_t xor_cs(const uint8_t *d, uint16_t len)
{
	uint8_t c = 0;
	for (uint16_t i = 0; i < len; i++) c ^= d[i];
	return c;
}

static void dispatch(uint8_t cmd, const uint8_t *p, uint16_t len)
{
	switch (cmd)
	{
	case CMD_CONNECT_IND:
		bp_connected = 1;
		bp_channel_open = 0;
		break;
	case CMD_CHANNELOPEN_RSP:
		if (len >= 8 && p[7] >= 19 && p[7] <= BP_MAX_PAYLOAD) bp_mps = p[7];
		bp_channel_open = 1;
		break;
	case CMD_DISCONNECT_IND:
		bp_connected = 0;
		bp_channel_open = 0;
		break;
	case CMD_DATA_IND:
		/* Payload = BTMAC(6) + RSSI(1) + Nutzdaten */
		if (!bp_data_ready && len > 7 && (uint16_t)(len - 7) <= BP_MAX_PAYLOAD)
		{
			memcpy(bp_data_buf, p + 7, len - 7);
			bp_data_len = len - 7;
			bp_data_ready = 1;
		}
		break;
	default:
		break;
	}
}

static void process_byte(uint8_t b)
{
	switch (pstate)
	{
	case ST_STX:  if (b == BLE_STX) { f_cs = b; pstate = ST_CMD; } break;
	case ST_CMD:  f_cmd = b; f_cs ^= b; pstate = ST_LEN_L; break;
	case ST_LEN_L: f_len = b; f_cs ^= b; pstate = ST_LEN_H; break;
	case ST_LEN_H:
		f_len |= (uint16_t)b << 8; f_cs ^= b; f_pos = 0;
		if (f_len > BP_MAX_PAYLOAD) pstate = ST_STX;
		else pstate = (f_len == 0) ? ST_CS : ST_PAYLOAD;
		break;
	case ST_PAYLOAD:
		f_payload[f_pos++] = b; f_cs ^= b;
		if (f_pos >= f_len) pstate = ST_CS;
		break;
	case ST_CS:
		if (b == f_cs) dispatch(f_cmd, f_payload, f_len);
		pstate = ST_STX;
		break;
	default: pstate = ST_STX; break;
	}
}

void BP_Poll(void)
{
	while (__HAL_UART_GET_FLAG(bp_uart, UART_FLAG_RXNE))
	{
		uint8_t b = (uint8_t)(bp_uart->Instance->RDR & 0xFF);
		process_byte(b);
	}
}

uint8_t BP_Send(const uint8_t *data, uint16_t len)
{
	uint8_t frame[BP_FRAME_MAX];
	uint16_t sent = 0;

	if (!bp_channel_open || bp_uart == NULL || len == 0) return 0;

	while (sent < len)
	{
		uint16_t chunk = len - sent;
		if (chunk > bp_mps) chunk = bp_mps;
		frame[0] = BLE_STX;
		frame[1] = CMD_DATA_REQ;
		frame[2] = (uint8_t)(chunk & 0xFF);
		frame[3] = (uint8_t)((chunk >> 8) & 0xFF);
		memcpy(&frame[4], data + sent, chunk);
		frame[4 + chunk] = xor_cs(frame, 4 + chunk);
		if (HAL_UART_Transmit(bp_uart, frame, 5 + chunk, 300) != HAL_OK) return 0;
		sent += chunk;
	}
	return 1;
}

uint8_t BP_SendStr(const char *s)
{
	return BP_Send((const uint8_t *)s, (uint16_t)strlen(s));
}

void BP_Init(UART_HandleTypeDef *huart)
{
	bp_uart = huart;
	pstate = ST_STX;
	bp_connected = 0;
	bp_channel_open = 0;
	bp_data_ready = 0;

	/* MODE low = Kommando-Modus; Reset-Sequenz (aktiv low). */
	HAL_GPIO_WritePin(BLE_MODE_GPIO_Port, BLE_MODE_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_SET);
	/* Modul bootet und advertised danach selbstständig. */
}
