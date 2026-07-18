/*
 * config_store.c
 *
 * Zwei-Pages-Ping-Pong-Konfigurationsspeicher fuer STM32G0B1 (siehe Header).
 *
 * Datensatzformat v2 (72 Bytes = 9 Flash-Doppelworte), jeweils am Page-Anfang:
 *   0..63   Config-Block (cfg_data, Layout siehe Header)
 *   64..67  Sequenzzaehler (uint32 LE), hoechster gueltiger Zaehler gewinnt
 *   68..69  Magic 0xA55A (LE)
 *   70..71  CRC16-CCITT ueber Bytes 0..69 (LE)
 *
 * Altformat v1 (40 Bytes): 32 Bytes Daten, Seq @32, Magic @36, CRC @38
 * (ueber 0..37). Wird beim Laden erkannt und einmalig nach v2 migriert.
 *
 * CFG_FLASH_BASE ist fuer Host-Tests ueberschreibbar (siehe tests/):
 * dort zeigt es auf einen RAM-Puffer statt auf 0x08000000.
 */

#include "config_store.h"
#include <string.h>

#ifndef CFG_FLASH_BASE
#include "stm32g0xx_hal.h"
#define CFG_FLASH_BASE   ((uintptr_t)0x08000000UL)
#endif

#define CFG_PAGE_A_NUM   62U
#define CFG_PAGE_B_NUM   63U
#define CFG_PAGE_SIZE    2048UL
#define CFG_PAGE_A_ADDR  (CFG_FLASH_BASE + CFG_PAGE_SIZE * CFG_PAGE_A_NUM)
#define CFG_PAGE_B_ADDR  (CFG_FLASH_BASE + CFG_PAGE_SIZE * CFG_PAGE_B_NUM)
#define CFG_MAGIC        0xA55AU
#define CFG_REC_SIZE     (CFG_SIZE + 8U)     /* 72: Daten + Seq + Magic + CRC */

/* Altformat v1 (bis FW 1.2.4) */
#define CFG_V1_SIZE      32U
#define CFG_V1_REC_SIZE  40U

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

static uintptr_t page_addr(uint8_t p)
{
	return p ? CFG_PAGE_B_ADDR : CFG_PAGE_A_ADDR;
}

static uint32_t rd_seq(const uint8_t *r, uint32_t off)
{
	return (uint32_t)r[off] | ((uint32_t)r[off + 1] << 8) |
	       ((uint32_t)r[off + 2] << 16) | ((uint32_t)r[off + 3] << 24);
}

/* Prueft einen v2-Datensatz (aktuelles Format). */
static uint8_t record_valid(uintptr_t addr, uint32_t *seq_out)
{
	const uint8_t *r = (const uint8_t *)addr;
	uint16_t magic = (uint16_t)(r[CFG_SIZE + 4] | (r[CFG_SIZE + 5] << 8));
	if (magic != CFG_MAGIC)
	{
		return 0;
	}
	uint16_t crc = (uint16_t)(r[CFG_SIZE + 6] | (r[CFG_SIZE + 7] << 8));
	if (crc16(r, CFG_SIZE + 6) != crc)
	{
		return 0;
	}
	*seq_out = rd_seq(r, CFG_SIZE);
	return 1;
}

/* Prueft einen v1-Datensatz (32-Byte-Altformat bis FW 1.2.4). */
static uint8_t record_valid_v1(uintptr_t addr, uint32_t *seq_out)
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
	*seq_out = rd_seq(r, 32);
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
	 * damit ein evtl. Altbestand in Page B unangetastet bleibt. */
	uint8_t target = (cur_page == 0) ? 1 : 0;
	uint32_t new_seq = cur_seq + 1;
	uint16_t crc;

	cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;	/* Layout-Version immer stempeln */

	memcpy(rec, cfg_data, CFG_SIZE);
	rec[CFG_SIZE + 0] = (uint8_t)(new_seq & 0xFF);
	rec[CFG_SIZE + 1] = (uint8_t)((new_seq >> 8) & 0xFF);
	rec[CFG_SIZE + 2] = (uint8_t)((new_seq >> 16) & 0xFF);
	rec[CFG_SIZE + 3] = (uint8_t)((new_seq >> 24) & 0xFF);
	rec[CFG_SIZE + 4] = (uint8_t)(CFG_MAGIC & 0xFF);
	rec[CFG_SIZE + 5] = (uint8_t)(CFG_MAGIC >> 8);
	crc = crc16(rec, CFG_SIZE + 6);
	rec[CFG_SIZE + 6] = (uint8_t)(crc & 0xFF);
	rec[CFG_SIZE + 7] = (uint8_t)(crc >> 8);

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

	/* 1) Aktuelles Format (v2) */
	uint8_t vA = record_valid(CFG_PAGE_A_ADDR, &seqA);
	uint8_t vB = record_valid(CFG_PAGE_B_ADDR, &seqB);

	if (vA || vB)
	{
		uint8_t use_b = (uint8_t)(vB && (!vA || ((int32_t)(seqB - seqA) > 0)));
		memcpy(cfg_data, (const void *)page_addr(use_b), CFG_SIZE);
		cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;
		cur_page = use_b;
		cur_seq = use_b ? seqB : seqA;
		return 1;
	}

	/* 2) Migration v1 -> v2: Datensatz im 32-Byte-Format (FW <= 1.2.4)?
	 * Bytes 0..31 (Kalibrierung, Tankform, Adresse, ...) unveraendert
	 * uebernehmen, Namensfeld leer. config_save() schreibt in die ANDERE
	 * Page - der v1-Datensatz bleibt als Backup liegen. */
	vA = record_valid_v1(CFG_PAGE_A_ADDR, &seqA);
	vB = record_valid_v1(CFG_PAGE_B_ADDR, &seqB);

	if (vA || vB)
	{
		uint8_t use_b = (uint8_t)(vB && (!vA || ((int32_t)(seqB - seqA) > 0)));
		memset(cfg_data, 0xFF, CFG_SIZE);
		memcpy(cfg_data, (const void *)page_addr(use_b), CFG_V1_SIZE);
		cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;
		cur_page = use_b;					/* Save zielt auf die andere Page */
		cur_seq = use_b ? seqB : seqA;		/* Seq laeuft weiter */
		config_save();
		return 1;
	}

	/* 3) Migration Ur-Altformat: 32-Byte-Block ohne Header in Page 63.
	 * Nur importieren, wenn der v1-Header-Bereich (32..39) unbeschrieben
	 * ist - das schliesst einen zerschossenen v1-Datensatz aus. */
	{
		const uint8_t *legacy = (const uint8_t *)CFG_PAGE_B_ADDR;
		uint8_t has_data = 0, hdr_clean = 1;

		for (uint8_t i = 0; i < CFG_V1_SIZE; i++)
		{
			if (legacy[i] != 0xFF) { has_data = 1; break; }
		}
		for (uint8_t i = CFG_V1_SIZE; i < CFG_V1_REC_SIZE; i++)
		{
			if (legacy[i] != 0xFF) { hdr_clean = 0; break; }
		}

		if (has_data && hdr_clean)
		{
			memset(cfg_data, 0xFF, CFG_SIZE);
			memcpy(cfg_data, legacy, CFG_V1_SIZE);
			cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;
			cur_page = 0xFF;
			cur_seq = 0;
			config_save();	/* schreibt nach Page A; Altbestand in B bleibt als Backup */
			return 1;
		}
	}

	memset(cfg_data, 0xFF, CFG_SIZE);
	cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;
	cur_page = 0xFF;
	cur_seq = 0;
	return 0;
}

uint8_t config_factory_reset(void)
{
	uint8_t ok = erase_page_num(CFG_PAGE_A_NUM);
	ok = (uint8_t)(ok & erase_page_num(CFG_PAGE_B_NUM));
	memset(cfg_data, 0xFF, CFG_SIZE);
	cfg_data[CFG_VER_OFF] = CFG_LAYOUT_V;
	cur_page = 0xFF;
	cur_seq = 0;
	return ok;
}

void config_nmi_recover(uint32_t fail_addr)
{
	uintptr_t fa = fail_addr;
	if ((fa >= CFG_PAGE_A_ADDR) && (fa < (CFG_PAGE_B_ADDR + CFG_PAGE_SIZE)))
	{
		erase_page_num((fa >= CFG_PAGE_B_ADDR) ? CFG_PAGE_B_NUM : CFG_PAGE_A_NUM);
		NVIC_SystemReset();
	}
}
