/*
 * Host-Test fuer den Config-Store (config_store.c) - laeuft auf dem PC,
 * kein Target noetig:
 *
 *     gcc -std=gnu11 -Wall -Wextra -o test_config_store tests/test_config_store.c
 *     ./test_config_store
 *
 * Der Flash wird durch einen RAM-Puffer ersetzt (CFG_FLASH_BASE-Hook in
 * config_store.c), die HAL-Flash-Funktionen durch Mocks mit NOR-Semantik
 * (Programmieren kann Bits nur loeschen). Getestet wird vor allem die
 * Migration v1 -> v2: Kalibrierung und alle Einstellungen muessen ein
 * Firmware-Update ueberleben.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Flash-Attrappe + HAL-Mock ---------------- */

static uint8_t mock_flash[64 * 2048];   /* Pages 0..63 (nur 62/63 genutzt) */

#define CFG_FLASH_BASE ((uintptr_t)mock_flash)

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;

typedef struct {
	uint32_t TypeErase;
	uint32_t Banks;
	uint32_t Page;
	uint32_t NbPages;
} FLASH_EraseInitTypeDef;

#define FLASH_TYPEERASE_PAGES        0U
#define FLASH_BANK_1                 1U
#define FLASH_TYPEPROGRAM_DOUBLEWORD 0U

static int unlock_cnt = 0;

static void HAL_FLASH_Unlock(void) { unlock_cnt++; }
static void HAL_FLASH_Lock(void)   { unlock_cnt--; }

static HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *er, uint32_t *perr)
{
	assert(unlock_cnt > 0);              /* Erase nur bei entsperrtem Flash */
	for (uint32_t p = 0; p < er->NbPages; p++)
	{
		memset(mock_flash + (er->Page + p) * 2048UL, 0xFF, 2048);
	}
	*perr = 0xFFFFFFFFUL;
	return HAL_OK;
}

static HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uintptr_t addr, uint64_t data)
{
	(void)type;
	assert(unlock_cnt > 0);
	assert((addr % 8) == 0);             /* Doppelwort-Ausrichtung */
	uint8_t *dst = (uint8_t *)addr;
	const uint8_t *src = (const uint8_t *)&data;
	for (int i = 0; i < 8; i++)
	{
		dst[i] &= src[i];                /* NOR-Semantik: nur 1 -> 0 moeglich */
	}
	return HAL_OK;
}

static void NVIC_SystemReset(void) { }

/* Zu testenden Code direkt einbinden (nutzt die Mocks oben) */
#include "../Core/Src/config_store.c"

/* ---------------- Hilfsfunktionen ---------------- */

#define PAGE_A (mock_flash + 62UL * 2048)
#define PAGE_B (mock_flash + 63UL * 2048)

static void flash_blank(void)
{
	memset(mock_flash, 0xFF, sizeof(mock_flash));
	cur_page = 0xFF;                     /* "Neustart" der Firmware simulieren */
	cur_seq = 0;
}

/* Baut einen gueltigen v1-Datensatz (40 Byte, altes Format) in 'dst'. */
static void write_v1_record(uint8_t *dst, const uint8_t *data32, uint32_t seq)
{
	uint8_t rec[40];
	memcpy(rec, data32, 32);
	rec[32] = (uint8_t)(seq & 0xFF);
	rec[33] = (uint8_t)((seq >> 8) & 0xFF);
	rec[34] = (uint8_t)((seq >> 16) & 0xFF);
	rec[35] = (uint8_t)((seq >> 24) & 0xFF);
	rec[36] = (uint8_t)(CFG_MAGIC & 0xFF);
	rec[37] = (uint8_t)(CFG_MAGIC >> 8);
	uint16_t c = crc16(rec, 38);
	rec[38] = (uint8_t)(c & 0xFF);
	rec[39] = (uint8_t)(c >> 8);
	memcpy(dst, rec, 40);
}

/* Beispieldaten wie auf einem echten, kalibrierten Sensor */
static void sample_v1_data(uint8_t *d)
{
	memset(d, 0xFF, 32);
	d[0] = 0x00;                         /* kalibriert                        */
	d[1] = 0x78; d[2] = 0x56; d[3] = 0x34; d[4] = 0x12;   /* max_val LE      */
	d[8] = 0x00;                         /* DAC-Kalibrierung vorhanden        */
	d[17] = 1;                           /* Fluidtyp Wasser                   */
	d[18] = 150;                         /* 150 L                             */
	for (int i = 0; i < 11; i++) d[19 + i] = (uint8_t)(i * 10);
	d[30] = 0x23;                        /* Quelladresse                      */
	d[31] = 2;                           /* Instanz                           */
}

static int tests = 0, failed = 0;
#define CHECK(cond) do { tests++; if (!(cond)) { failed++; \
	printf("FEHLER Zeile %d: %s\n", __LINE__, #cond); } } while (0)

/* ---------------- Testfaelle ---------------- */

int main(void)
{
	uint8_t v1[32];
	uint32_t seq;

	/* 1) Leerer Flash: nichts zu laden, Defaults */
	flash_blank();
	CHECK(config_load() == 0);
	CHECK(cfg_data[0] == 0xFF);
	CHECK(cfg_data[CFG_VER_OFF] == CFG_LAYOUT_V);

	/* 2) v2-Roundtrip: speichern, "Neustart", laden */
	flash_blank();
	config_load();
	cfg_data[0] = 0x00;
	cfg_data[1] = 0xAA;
	memcpy(&cfg_data[CFG_NAME_OFF], "Frischwasser Bug\0", 17);
	CHECK(config_save() == 1);
	uint8_t before[CFG_SIZE];
	memcpy(before, cfg_data, CFG_SIZE);
	memset(cfg_data, 0, CFG_SIZE);
	cur_page = 0xFF; cur_seq = 0;        /* Neustart */
	CHECK(config_load() == 1);
	CHECK(memcmp(cfg_data, before, CFG_SIZE) == 0);

	/* 3) Kernfall - Migration v1 -> v2 (Firmware-Update):
	 *    Kalibrierung und alle Einstellungen muessen erhalten bleiben. */
	flash_blank();
	sample_v1_data(v1);
	write_v1_record(PAGE_A, v1, 5);
	CHECK(config_load() == 1);
	CHECK(memcmp(cfg_data, v1, 32) == 0);            /* Bytes 0..31 unveraendert  */
	CHECK(cfg_data[CFG_VER_OFF] == CFG_LAYOUT_V);    /* Versionsbyte gesetzt      */
	CHECK(cfg_data[CFG_NAME_OFF] == 0xFF);           /* Name noch leer            */
	CHECK(record_valid((uintptr_t)PAGE_B, &seq) == 1);   /* v2 in der ANDEREN Page */
	CHECK(seq == 6);                                 /* Sequenz laeuft weiter     */
	CHECK(record_valid_v1((uintptr_t)PAGE_A, &seq) == 1);/* v1-Backup unangetastet */
	/* ... und nach einem weiteren Neustart wird direkt v2 geladen: */
	memset(cfg_data, 0, CFG_SIZE);
	cur_page = 0xFF; cur_seq = 0;
	CHECK(config_load() == 1);
	CHECK(memcmp(cfg_data, v1, 32) == 0);

	/* 4) Zwei gueltige v2-Datensaetze: hoehere Sequenz gewinnt (auch bei Wrap) */
	flash_blank();
	config_load();
	cfg_data[1] = 0x11;
	CHECK(config_save() == 1);           /* seq 1 -> Page A */
	cfg_data[1] = 0x22;
	CHECK(config_save() == 1);           /* seq 2 -> Page B */
	cur_page = 0xFF; cur_seq = 0;
	CHECK(config_load() == 1);
	CHECK(cfg_data[1] == 0x22);
	/* Wrap: 0xFFFFFFFF in A, 0 in B -> B ist neuer */
	flash_blank();
	sample_v1_data(v1);
	{
		uint8_t recA[CFG_REC_SIZE], recB[CFG_REC_SIZE];
		memset(recA, 0xFF, sizeof(recA)); memset(recB, 0xFF, sizeof(recB));
		recA[1] = 0xA1; recB[1] = 0xB2;
		recA[CFG_VER_OFF] = 2; recB[CFG_VER_OFF] = 2;
		uint32_t sA = 0xFFFFFFFFUL, sB = 0;
		memcpy(&recA[CFG_SIZE], &sA, 4); memcpy(&recB[CFG_SIZE], &sB, 4);
		recA[CFG_SIZE+4] = 0x5A; recA[CFG_SIZE+5] = 0xA5;
		recB[CFG_SIZE+4] = 0x5A; recB[CFG_SIZE+5] = 0xA5;
		uint16_t cA = crc16(recA, CFG_SIZE+6), cB = crc16(recB, CFG_SIZE+6);
		memcpy(&recA[CFG_SIZE+6], &cA, 2); memcpy(&recB[CFG_SIZE+6], &cB, 2);
		memcpy(PAGE_A, recA, CFG_REC_SIZE); memcpy(PAGE_B, recB, CFG_REC_SIZE);
	}
	cur_page = 0xFF; cur_seq = 0;
	CHECK(config_load() == 1);
	CHECK(cfg_data[1] == 0xB2);

	/* 5) Ur-Altformat (nackte 32 Byte in Page 63) wird weiterhin migriert */
	flash_blank();
	sample_v1_data(v1);
	memcpy(PAGE_B, v1, 32);
	CHECK(config_load() == 1);
	CHECK(memcmp(cfg_data, v1, 32) == 0);
	CHECK(record_valid((uintptr_t)PAGE_A, &seq) == 1);   /* v2 nach Page A */

	/* 6) Kaputte CRC (v2 und v1) wird ignoriert */
	flash_blank();
	sample_v1_data(v1);
	write_v1_record(PAGE_A, v1, 7);
	PAGE_A[3] ^= 0x01;                   /* Datenbyte kippen -> CRC falsch */
	CHECK(config_load() == 0);
	CHECK(cfg_data[0] == 0xFF);

	/* 7) Flash immer wieder gesperrt (Lock/Unlock ausgeglichen) */
	CHECK(unlock_cnt == 0);

	printf("%d Tests, %d Fehler\n", tests, failed);
	return failed ? 1 : 0;
}
