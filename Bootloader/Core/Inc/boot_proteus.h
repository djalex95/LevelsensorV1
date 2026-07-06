/*
 * boot_proteus.h
 *
 * Abgespeckter Proteus-e-UART-Treiber für den Bootloader (Polling, ohne
 * Interrupts). Nimmt im DFU-Modus die BLE-Datenpakete entgegen und sendet
 * Antworten zurück.
 */

#ifndef INC_BOOT_PROTEUS_H_
#define INC_BOOT_PROTEUS_H_

#include "stm32g0xx_hal.h"

#define BP_MAX_PAYLOAD 243

/* Verbindungszustand */
extern volatile uint8_t bp_connected;
extern volatile uint8_t bp_channel_open;

/* Empfangenes Datenpaket (Nutzdaten ohne BTMAC/RSSI) */
extern volatile uint8_t  bp_data_ready;
extern uint8_t           bp_data_buf[BP_MAX_PAYLOAD];
extern volatile uint16_t bp_data_len;

/* Modul zurücksetzen (Reset-Sequenz) und UART-Empfang vorbereiten. */
void BP_Init(UART_HandleTypeDef *huart);

/* In der Hauptschleife aufrufen: liest vorhandene UART-Bytes und parst Frames. */
void BP_Poll(void);

/* Nutzdaten an das Handy senden (CMD_DATA_REQ, ggf. gestückelt). */
uint8_t BP_Send(const uint8_t *data, uint16_t len);
uint8_t BP_SendStr(const char *s);

#endif /* INC_BOOT_PROTEUS_H_ */
