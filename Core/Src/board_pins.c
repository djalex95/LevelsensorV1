/*
 * board_pins.c - Beschreibung und Begruendung siehe board_pins.h
 */

#include "board_pins.h"
#include "main.h"
#include "sensor_cfg.h"		/* liefert HW_VARIANT und prueft es gegen den Include-Pfad */

#if HW_VARIANT == 1000

/* ---------------- Platine V1 (STM32G0B1KBU6N) ---------------- */

void board_fdcan_pins_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* Erst die von CubeMX gesetzten V2-Pins wieder loesen. Ohne das waeren
	 * PA11 und PD0 gleichzeitig auf FDCAN1_RX gemuxt. PA11 haengt auf der
	 * V1-Platine an keinem Netz (die Loetbruecken zu den CAN-Leitungen sind
	 * offen), der Eingang wuerde also frei floaten und den Empfang stoeren. */
	HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);

	__HAL_RCC_GPIOD_CLK_ENABLE();

	/* PD0 -> FDCAN1_RX, PD1 -> FDCAN1_TX; gleiche Alternate Function wie
	 * auf PA11/PA12, es aendern sich nur Port und Pinmaske. */
	GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF3_FDCAN1;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void board_gpio_fixup(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOD_CLK_ENABLE();

	/* CAN_STBY liegt auf PD2 statt auf PB4, mit demselben Startpegel wie in
	 * der generierten Init: LOW = Transceiver aktiv. Der Pin wird im Betrieb
	 * nirgends umgeschaltet, deshalb genuegt diese einmalige Konfiguration
	 * und es braucht keine Makro-Indirektion im uebrigen Code. */
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);

	GPIO_InitStruct.Pin = GPIO_PIN_2;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

#else

/* ------------- Platine V2 (STM32G0B1KBU6, 1001/1003) -------------
 * Die generierte Init passt bereits, hier ist nichts nachzuziehen. */

void board_fdcan_pins_init(void)
{
}

void board_gpio_fixup(void)
{
}

#endif
