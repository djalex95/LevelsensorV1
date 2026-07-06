/*
 * main.c – OTA-Bootloader (STM32G0B1KB)
 *
 * Beim Start: prüft Enter-DFU-Magic und App-Gültigkeit. Entweder in die App
 * springen oder in den DFU-Empfangsmodus gehen (Proteus-e). Siehe
 * Bootloader/DESIGN.md.
 */

#include "main.h"
#include "dfu_common.h"
#include "boot_dfu.h"
#include "boot_proteus.h"
#include <string.h>

UART_HandleTypeDef huart2;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void Error_Handler(void);
static void led_update(void);

static uint32_t last_data_tick;   /* Zeitpunkt des letzten DFUD-Pakets */

/* ---- App-Bereich: Seiten 16..60 (0x08008000..0x0801E7FF), Metadaten Seite 61 ---- */
#define APP_PAGE_FIRST   16
#define APP_PAGE_COUNT   45
#define META_PAGE        61

/* ---- Flash-Schnittstelle für boot_dfu ---- */

static int fl_erase(void)
{
	FLASH_EraseInitTypeDef er = {0};
	uint32_t perr = 0;
	HAL_StatusTypeDef st;

	HAL_FLASH_Unlock();
	er.TypeErase = FLASH_TYPEERASE_PAGES;
	er.Banks = FLASH_BANK_1;
	er.Page = APP_PAGE_FIRST;
	er.NbPages = APP_PAGE_COUNT;
	st = HAL_FLASHEx_Erase(&er, &perr);
	if (st == HAL_OK)
	{
		/* Metadaten-Seite mitlöschen -> alter Gültigkeits-Marker verschwindet,
		 * die App gilt während des Updates als ungültig. */
		er.Page = META_PAGE;
		er.NbPages = 1;
		st = HAL_FLASHEx_Erase(&er, &perr);
	}
	HAL_FLASH_Lock();
	return (st == HAL_OK) ? 0 : -1;
}

static int fl_write(uint32_t offset, const uint8_t *data, uint32_t len)
{
	int r = 0;
	HAL_FLASH_Unlock();
	for (uint32_t i = 0; i < len; i += 8)
	{
		uint64_t dw;
		memcpy(&dw, data + i, 8);
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
		                      DFU_APP_ADDR + offset + i, dw) != HAL_OK)
		{
			r = -1;
			break;
		}
	}
	HAL_FLASH_Lock();
	return r;
}

static uint32_t fl_crc(uint32_t size)
{
	return dfu_crc32((const uint8_t *)DFU_APP_ADDR, size);
}

static int fl_meta(uint32_t size, uint32_t crc)
{
	dfu_meta_t m;
	m.magic = DFU_META_MAGIC;
	m.app_size = size;
	m.app_crc32 = crc;
	m.reserved = 0xFFFFFFFFUL;

	int r = 0;
	const uint8_t *mp = (const uint8_t *)&m;
	HAL_FLASH_Unlock();
	for (uint32_t i = 0; i < sizeof(m); i += 8)
	{
		uint64_t dw;
		memcpy(&dw, mp + i, 8);
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
		                      DFU_META_ADDR + i, dw) != HAL_OK)
		{
			r = -1;
			break;
		}
	}
	HAL_FLASH_Lock();
	return r;
}

static const dfu_flash_ops_t flash_ops = { fl_erase, fl_write, fl_crc, fl_meta };

/* ---- App-Gültigkeit und Sprung ---- */

static uint8_t app_valid(void)
{
	const dfu_meta_t *m = (const dfu_meta_t *)DFU_META_ADDR;
	if (m->magic != DFU_META_MAGIC) return 0;
	if (m->app_size == 0 || m->app_size > DFU_APP_MAX) return 0;
	return (dfu_crc32((const uint8_t *)DFU_APP_ADDR, m->app_size) == m->app_crc32);
}

static void jump_to_app(void)
{
	uint32_t sp = *(volatile uint32_t *)DFU_APP_ADDR;
	uint32_t pc = *(volatile uint32_t *)(DFU_APP_ADDR + 4);

	__disable_irq();
	HAL_RCC_DeInit();
	HAL_DeInit();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	SCB->VTOR = DFU_APP_ADDR;
	__set_MSP(sp);
	__enable_irq();
	((void (*)(void))pc)();
	while (1) { }
}

/* ---- main ---- */

int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();

	uint8_t dfu_req = (*DFU_REQ_ADDR == DFU_REQ_MAGIC);
	*DFU_REQ_ADDR = 0;

	if (!dfu_req && app_valid())
	{
		jump_to_app();   /* kehrt nicht zurück */
	}

	/* --- DFU-Modus --- */
	MX_USART2_UART_Init();
	dfu_init(&flash_ops);
	BP_Init(&huart2);

	char resp[64];
	uint8_t do_reset;
	last_data_tick = (uint32_t)(-1000);   /* Start: weißes Blinken */

	while (1)
	{
		BP_Poll();
		led_update();

		if (bp_data_ready)
		{
			/* Während der Datenübertragung (DFUD) blau blinken */
			if (bp_data_len >= 4 && memcmp(bp_data_buf, DFU_TAG_DATA, 4) == 0)
			{
				last_data_tick = HAL_GetTick();
			}

			uint16_t n = dfu_on_data(bp_data_buf, bp_data_len, resp, &do_reset);
			bp_data_ready = 0;
			if (n > 0)
			{
				BP_SendStr(resp);
			}
			if (do_reset)
			{
				HAL_Delay(200);        /* Antwort noch rausgehen lassen */
				NVIC_SystemReset();    /* Neustart -> gültige App wird gestartet */
			}
		}
	}
}

/* ---- Peripherie-Init (aus dem App-Projekt übernommen) ---- */

void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
	huart2.Instance = USART2;
	huart2.Init.BaudRate = 115200;
	huart2.Init.WordLength = UART_WORDLENGTH_8B;
	huart2.Init.StopBits = UART_STOPBITS_1;
	huart2.Init.Parity = UART_PARITY_NONE;
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
	if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
	if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
	if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef gi = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/* BLE_RESET (PA5) – Ausgang, Reset zunächst aktiv (low) */
	HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_RESET);
	gi.Pin = BLE_RESET_Pin;
	gi.Mode = GPIO_MODE_OUTPUT_PP;
	gi.Pull = GPIO_NOPULL;
	gi.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(BLE_RESET_GPIO_Port, &gi);

	/* BLE_MODE (PB9) – Ausgang, low = Kommando-Modus */
	HAL_GPIO_WritePin(BLE_MODE_GPIO_Port, BLE_MODE_Pin, GPIO_PIN_RESET);
	gi.Pin = BLE_MODE_Pin;
	HAL_GPIO_Init(BLE_MODE_GPIO_Port, &gi);

	/* BLE_BUSY (PB1) – Eingang */
	gi.Pin = BLE_BUSY_Pin;
	gi.Mode = GPIO_MODE_INPUT;
	gi.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(BLE_BUSY_GPIO_Port, &gi);

	/* RGB-LED als einfache Ausgänge (aktiv-low): PA6 rot, PA7 grün, PB0 blau.
	 * High = aus. Der Bootloader nutzt sie nur zum Blinken (kein PWM). */
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
	gi.Pin = GPIO_PIN_6 | GPIO_PIN_7;
	gi.Mode = GPIO_MODE_OUTPUT_PP;
	gi.Pull = GPIO_NOPULL;
	gi.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &gi);
	gi.Pin = GPIO_PIN_0;
	HAL_GPIO_Init(GPIOB, &gi);
}

/* Status-LED: weißes Blinken im Bootloader, blaues Blinken während der
 * Datenübertragung. LED ist aktiv-low (Pin low = an). */
#define LED_R_OFF()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET)
#define LED_G_OFF()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET)
#define LED_B_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)
#define LED_R_ON()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET)
#define LED_G_ON()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET)
#define LED_B_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)

static void led_update(void)
{
	uint32_t now = HAL_GetTick();
	uint8_t on = ((now / 250) & 1);                   /* 250 ms an/aus         */
	uint8_t transferring = ((now - last_data_tick) < 800);

	if (!on)
	{
		LED_R_OFF(); LED_G_OFF(); LED_B_OFF();
		return;
	}
	if (transferring)
	{
		LED_R_OFF(); LED_G_OFF(); LED_B_ON();         /* blau                  */
	}
	else
	{
		LED_R_ON(); LED_G_ON(); LED_B_ON();           /* weiß                  */
	}
}

void Error_Handler(void)
{
	__disable_irq();
	while (1) { }
}
