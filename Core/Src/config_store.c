/*
 * config_store.c
 *
 * Zwei-Pages-Ping-Pong-Konfigurationsspeicher fuer STM32G0B1 (siehe Header).
 *
 * Datensatzformat (40 Bytes = 5 Flash-Doppelworte), jeweils am Page-Anfang:
 *   0..31   Config-Block (cfg_data)
 *   32..35  Sequenzzaehler (uint32 LE), hoechster gueltiger Zaehler gewinnt
 *   36..37  Magic 0xA55A (LE)
 *   38..39  CRC16-CCITT ueber Bytes 0..37 (LE)
 */

#include "config_store.h"
#include "stm32g0xx_hal.h"
#include <string.h>

#define CFG_PAGE_A_NUM   62U
#define CFG_PAGE_B_NUM   63U
#define CFG_PAGE_SIZE    2048UL
#define CFG_PAGE_A_ADDR  (0x08000000UL + CFG_PAGE_SIZE * CFG_PAGE_A_NUM)
#define CFG_PAGE_B_ADDR  (0x08000000UL + CFG_PAGE_SIZE * CFG_PAGE_B_NUM)
#define CFG_MAGIC        0xA55AU
#define CFG_REC_SIZE     40U

uint8_t cfg_data[CFG_SIZE];

static uint8_t cur_page = 0xFF;		/* 0 = Page A, 1 = Page B, 0xFF = keine */
static uint32_t cur_seq = 0;

/* ------------------------------------------------------------------ intern */

static uint16_t crc16(const uint8_t *d, uint32_t len)
{
	uint16_t crc = 0xFFFF;
	while (len--)
	{
		crc ^= (uint16_t)((uint16_t)(*d++) << 8);
		for (uint8_t i = 0; i < 8; i++)
		{
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

static uint32_t page_addr(uint8_t p)
{
	return p ? CFG_PAGE_B_ADDR : CFG_PAGE_A_ADDR;
}

static uint8_t record_valid(uint32_t addr, uint32_t *seq_out)
{
	const uint8_t *r = (const uint8_t *)addr;
	uint16_t magic = (uint16_t)(r[36] | (r[37] << 8));
	if (magic != CFG_MAGIC)
	{
		return 0;
	}
	uint16_t crc = (uint16_t)(r[38] | (r[39] << 8));
	if (crc16(r, 38) != crc)
	{
		return 0;
	}
	*seq_out = (uint32_t)r[32] | ((uint32_t)r[33] << 8) | ((uint32_t)r[34] << 16) | ((uint32_t)r[35] << 24);
	return 1;
}

static uint8_t erase_page_num(uint32_t pnum)
{
	FLASH_EraseInitTypeDef er;
	uint32_t perr = 0;
	HAL_StatusTypeDef st;

	er.TypeErase = FLASH_TYPEERASE_PAGES;
	er.Banks = FLASH_BANK_1;
	er.Page = pnum;
	er.NbPages = 1;

	HAL_FLASH_Unlock();
	st = HAL_FLASHEx_Erase(&er, &perr);
	HAL_FLASH_Lock();

	return (uint8_t)((st == HAL_OK) && (perr == 0xFFFFFFFFUL));
}

/* ------------------------------------------------------------------ API */

uint8_t config_save(void)
{
	uint8_t rec[CFG_REC_SIZE];
	/* Ziel ist immer die andere Page; bei Erstbeschreibung (0xFF) Page A,
	 * damit ein evtl. Altformat-Bestand in Page B unangetastet bleibt. */
	uint8_t target = (cur_page == 0) ? 1 : 0;
	uint32_t new_seq = cur_seq + 1;
	uint16_t crc;

	memcpy(rec, cfg_data, CFG_SIZE);
	rec[32] = (uint8_t)(new_seq & 0xFF);
	rec[33] = (uint8_t)((new_seq >> 8) & 0xFF);
	rec[34] = (uint8_t)((new_seq >> 16) & 0xFF);
	rec[35] = (uint8_t)((new_seq >> 24) & 0xFF);
	rec[36] = (uint8_t)(CFG_MAGIC & 0xFF);
	rec[37] = (uint8_t)(CFG_MAGIC >> 8);
	crc = crc16(rec, 38);
	rec[38] = (uint8_t)(crc & 0xFF);
	rec[39] = (uint8_t)(crc >> 8);

	if (erase_page_num(target ? CFG_PAGE_B_NUM : CFG_PAGE_A_NUM) == 0)
	{
		return 0;
	}

	HAL_FLASH_Unlock();
	for (uint32_t i = 0; i < CFG_REC_SIZE; i += 8)
	{
		uint64_t dw;
		memcpy(&dw, &rec[i], 8);
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, page_addr(target) + i, dw) != HAL_OK)
		{
			HAL_FLASH_Lock();
			return 0;	/* alter Datensatz in der anderen Page bleibt gueltig */
		}
	}
	HAL_FLASH_Lock();

	/* Readback-Verifikation */
	if (memcmp((const void *)page_addr(target), rec, CFG_REC_SIZE) != 0)
	{
		return 0;
	}

	cur_page = target;
	cur_seq = new_seq;
	return 1;
}

uint8_t config_load(void)
{
	uint32_t seqA = 0, seqB = 0;
	uint8_t vA = record_valid(CFG_PAGE_A_ADDR, &seqA);
	uint8_t vB = record_valid(CFG_PAGE_B_ADDR, &seqB);

	if (vA || vB)
	{
		uint8_t use_b = (uint8_t)(vB && (!vA || ((int32_t)(seqB - seqA) > 0)));
		memcpy(cfg_data, (const void *)page_addr(use_b), CFG_SIZE);
		cur_page = use_b;
		cur_seq = use_b ? seqB : seqA;
		return 1;
	}

	/* Migration: Altformat (32-Byte-Block ohne Header) in Page 63?
	 * Nur importieren, wenn der Header-Bereich (32..39) unbeschrieben ist -
	 * das schliesst einen zerschossenen Neuformat-Datensatz aus. */
	{
		const uint8_t *legacy = (const uint8_t *)CFG_PAGE_B_ADDR;
		uint8_t has_data = 0, hdr_clean = 1;

		for (uint8_t i = 0; i < CFG_SIZE; i++)
		{
			if (legacy[i] != 0xFF) { has_data = 1; break; }
		}
		for (uint8_t i = CFG_SIZE; i < CFG_REC_SIZE; i++)
		{
			if (legacy[i] != 0xFF) { hdr_clean = 0; break; }
		}

		if (has_data && hdr_clean)
		{
			memcpy(cfg_data, legacy, CFG_SIZE);
			cur_page = 0xFF;
			cur_seq = 0;
			config_save();	/* schreibt nach Page A; Altbestand in B bleibt als Backup */
			return 1;
		}
	}

	memset(cfg_data, 0xFF, CFG_SIZE);
	cur_page = 0xFF;
	cur_seq = 0;
	return 0;
}

void config_nmi_recover(uint32_t fail_addr)
{
	if ((fail_addr >= CFG_PAGE_A_ADDR) && (fail_addr < (CFG_PAGE_B_ADDR + CFG_PAGE_SIZE)))
	{
		erase_page_num((fail_addr >= CFG_PAGE_B_ADDR) ? CFG_PAGE_B_NUM : CFG_PAGE_A_NUM);
		NVIC_SystemReset();
	}
}
