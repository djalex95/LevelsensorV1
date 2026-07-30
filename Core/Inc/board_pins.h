/*
 * board_pins.h - Pinbelegung, die sich zwischen den Platinen unterscheidet
 *
 * Die CubeMX-Generierung kennt genau eine Pinbelegung: die der Platine V2
 * (STM32G0B1KBU6, Varianten 1001 und 1003). Die Platine V1 (Variante 1000)
 * ist mit dem STM32G0B1KBU6N bestueckt, und bei diesem Typ liegen an
 * denselben Gehaeusepads andere Ports:
 *
 *   Pad   Platine V2 (KBU6)     Platine V1 (KBU6N)
 *   17    PB2   BLE_LED         PB15  BLE_LED
 *   22    PA11  FDCAN1_RX       PA11  (nur ueber Loetbruecke belegt)
 *   23    PA12  FDCAN1_TX       PA12  (nur ueber Loetbruecke belegt)
 *   26    PA15  WC (EEPROM)     PD0   FDCAN1_RX
 *   27    PB3   D_OUT           PD1   FDCAN1_TX
 *   28    PB4   CAN_STBY        PD2   CAN_STBY
 *
 * Die beiden Pinsaetze ueberschneiden sich nicht: PB2, PA15, PB3 und PB4
 * sind beim KBU6N gar nicht herausgefuehrt, PB15, PD0, PD1 und PD2 beim
 * KBU6 nicht. Deshalb darf die generierte Init unveraendert durchlaufen -
 * auf der jeweils anderen Platine schreibt sie in Pads, die es physisch
 * nicht gibt. Nachgezogen werden muss nur, was wirklich gebraucht wird,
 * und genau das tun die beiden Funktionen hier. Sie werden aus den von
 * CubeMX geschuetzten USER-CODE-Bloecken aufgerufen, damit eine
 * Neugenerierung des Projekts weiterhin erlaubt bleibt.
 *
 * Bewusst NICHT nachgezogen wird BLE_LED: der Verbindungszustand kommt
 * ausschliesslich aus den UART-Meldungen des Proteus-e (ble.c), der Pin
 * wird an keiner Stelle gelesen. Ebenso WC und D_OUT - beide gibt es auf
 * der V1-Platine nicht (kein EEPROM bestueckt), und die Firmware nutzt
 * sie ohnehin nur in der Init.
 */
#ifndef INC_BOARD_PINS_H_
#define INC_BOARD_PINS_H_

/* Aufruf aus USER CODE BEGIN FDCAN1_MspInit 1 (stm32g0xx_hal_msp.c),
 * also unmittelbar nachdem CubeMX die CAN-Pins der V2-Platine gesetzt
 * hat. Korrigiert sie auf die tatsaechliche Platine. */
void board_fdcan_pins_init(void);

/* Aufruf aus USER CODE BEGIN MX_GPIO_Init_2 (main.c), am Ende der
 * generierten GPIO-Init. Korrigiert die uebrigen abweichenden Pins. */
void board_gpio_fixup(void);

#endif /* INC_BOARD_PINS_H_ */
