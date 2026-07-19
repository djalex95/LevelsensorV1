/*
 * ble.h
 *
 * Treiber für das Würth Elektronik Proteus-e (2612011024000) an USART2.
 * Implementiert das Proteus-UART-Kommandoprotokoll (SPP-like Profil) und
 * stellt einen transparenten Datenkanal für eine Handy-App bereit.
 *
 *  Created on: Jun 9, 2024
 *      Author: a_hae
 */

#ifndef INC_BLE_H_
#define INC_BLE_H_

#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_uart.h"

/* Proteus-UART-Frame: 0x02 | CMD | Len(2, LE) | Payload | CS(XOR aller vorherigen)
 * Kommando-Schema: REQ = base, CNF = base|0x40, IND = base|0x80, RSP = base|0xC0.
 * HINWEIS: Werte gegen das Proteus-e User Manual gegenprüfen, falls ein Kommando
 * nicht wie erwartet reagiert. */
#define BLE_STX                 0x02
#define CMD_RESET_REQ           0x00
#define CMD_GETSTATE_REQ        0x01
#define CMD_GETSTATE_CNF        0x41
#define CMD_SLEEP_REQ           0x02
#define CMD_DATA_REQ            0x04
#define CMD_DATA_CNF            0x44
#define CMD_DATA_IND            0x84
#define CMD_CONNECT_IND         0x86
#define CMD_DISCONNECT_IND      0x87
#define CMD_SECURITY_IND        0x88
#define CMD_CHANNELOPEN_RSP     0xC6
#define CMD_TXCOMPLETE_RSP      0xC4
#define CMD_SET_REQ             0x11
#define CMD_GET_REQ             0x10	/* Setting lesen                       */
#define CMD_GET_CNF             0x50	/* Antwort: Status(1) + Wert           */
#define CMD_DISCONNECT_REQ      0x07
#define CMD_DELETEBONDS_REQ     0x0E	/* Bonding-Daten loeschen (Len 0=alle) */
#define CFG_IDX_DEVICENAME      0x02	/* Settings-Index RF_DeviceName        */
#define CFG_IDX_SECFLAGS        0x0C	/* Settings-Index RF_SecFlags (1 Byte) */
#define CFG_IDX_STATICPASSKEY   0x12	/* Settings-Index RF_StaticPasskey     */

/* Ziel-Sicherheitsmodus: Static Passkey (0x3) + Bonding (Bit 3) -> 0x0B.
 * Quelle: Proteus-e User Manual rev 1.9, Kap. 10.8 (Table 20). */
#define BLE_SECFLAGS_TARGET     0x0B

/* Werks-Passkey des Moduls (RF_StaticPasskey-Default laut Manual Kap. 10.7).
 * Gilt, solange im Config keine eigene PIN gespeichert ist. */
#define BLE_PIN_DEFAULT         "123123"
#define BLE_PIN_LEN             6

#define BLE_MAX_PAYLOAD         243
#define BLE_FRAME_MAX           (BLE_MAX_PAYLOAD + 5)

/* Verbindungszustand (in der ISR gesetzt, in der Hauptschleife gelesen) */
extern volatile uint8_t ble_connected;      /* 1 zwischen CONNECT_IND und DISCONNECT_IND */
extern volatile uint8_t ble_channel_open;   /* 1 nach CHANNELOPEN_RSP – erst dann senden */

/* Empfangene Nutzdaten (CMD_DATA_IND) für die Hauptschleife */
extern volatile uint8_t ble_data_ready;
extern uint8_t          ble_data_buf[BLE_MAX_PAYLOAD];
extern volatile uint16_t ble_data_len;

/* Modul zurücksetzen und UART-Empfang starten. */
void BLE_Init(UART_HandleTypeDef *huart);

/* Einzelnes empfangenes UART-Byte in den Frame-Parser geben (aus RX-Callback). */
void BLE_ProcessByte(uint8_t b);

/* Nutzdaten über den offenen Kanal an die App senden (CMD_DATA_REQ).
 * Rückgabe 0 = nicht gesendet (Kanal zu / Modul busy / Fehler). */
uint8_t BLE_SendData(const uint8_t *data, uint16_t len);

/* Komfort: nullterminierten Text senden. */
uint8_t BLE_SendString(const char *s);

/* Ändert den BLE-Gerätenamen (RF_DeviceName) dauerhaft im Modul-Flash.
 * Da CMD_SET_REQ nur im getrennten Zustand erlaubt ist, wird bei bestehender
 * Verbindung zuerst getrennt; die Hauptschleife wendet den Namen danach an.
 * Das Modul startet anschließend selbstständig neu. */
uint8_t BLE_SetDeviceName(const char *name);

/* Muss in der Hauptschleife aufgerufen werden, wenn ble_setname_pending gesetzt
 * ist und die Verbindung getrennt wurde (ble_connected == 0). */
void BLE_ApplyPendingName(void);

/* 1 = es liegt eine aufgeschobene Namensänderung vor (nach Trennung anwenden). */
extern volatile uint8_t ble_setname_pending;

/* Fragt eine Moduleinstellung ab (CMD_GET_REQ). Da die Antwort (CMD_GET_CNF)
 * den Settings-Index nicht enthält, wird er hier gemerkt - es darf immer nur
 * EINE Anfrage offen sein. Ergebnis landet asynchron in ble_get_value/-_len;
 * ble_get_ready wird dann 1. Rückgabe 0 = Sendefehler. */
uint8_t BLE_RequestSetting(uint8_t idx);

extern volatile uint8_t  ble_get_ready;
extern uint8_t           ble_get_index;      /* Index der offenen Anfrage    */
extern uint8_t           ble_get_value[21];  /* Wert (nullterminiert)        */
extern volatile uint16_t ble_get_len;

/* Einmalige Sicherheits-Provisionierung (nach Werksreset/Erstboot): setzt den
 * Sicherheitsmodus (RF_SecFlags) und den Passkey, löscht die Bond-Tabelle und
 * startet das Modul neu - alles in einem Rutsch (ein Modul-Reset). Danach wird
 * die Sicherheit im Betrieb NICHT mehr angefasst; die PIN ändert nur noch das
 * PIN-Kommando aktiv. Nur im getrennten Zustand aufrufen. */
uint8_t BLE_ProvisionSecurity(uint8_t flags, const char *pin);

/* Aktive Verbindung modulseitig trennen (CMD_DISCONNECT_REQ). */
void BLE_Disconnect(void);

/* Ändert den BLE-Passkey (RF_StaticPasskey, 6 Ziffern) dauerhaft im Modul,
 * löscht die Bonds und startet das Modul neu. Bei bestehender Verbindung
 * wird erst getrennt; die Hauptschleife wendet die PIN danach an
 * (ble_setpin_pending, BLE_ApplyPendingPin). */
uint8_t BLE_SetPin(const char *pin);
void BLE_ApplyPendingPin(void);
extern volatile uint8_t ble_setpin_pending;

#endif /* INC_BLE_H_ */
