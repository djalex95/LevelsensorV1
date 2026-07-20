/*
 * dfu_common.c
 *
 * Gemeinsame CRC32-Routine für das OTA-Update (App und Bootloader).
 * IEEE/zlib-CRC32 – identisch zur Berechnung in der Handy-App.
 *
 * Tabellenbasiert (256 Einträge, einmalig zur Laufzeit erzeugt): rechnet
 * ein Byte pro Schritt statt bitweise und ist damit rund 8x schneller -
 * spuerbar beim Boot (app_valid rechnet die CRC ueber die ganze App) und
 * bei der Update-Verifikation. Der Rueckgabewert ist bit-identisch zur
 * alten bitweisen Variante (Poly 0xEDB88320, reflektiert), die Pruefsummen
 * bleiben also kompatibel. Bewusst HAL-frei, damit dfu_common host-testbar
 * bleibt (kein Zugriff auf die CRC-Hardware).
 */

#include "dfu_common.h"

static uint32_t crc_table[256];
static uint8_t  crc_table_ready = 0;

static void crc_table_init(void)
{
	for (uint32_t i = 0; i < 256; i++)
	{
		uint32_t c = i;
		for (uint8_t b = 0; b < 8; b++)
		{
			c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
		}
		crc_table[i] = c;
	}
	crc_table_ready = 1;
}

uint32_t dfu_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	if (!crc_table_ready)
	{
		crc_table_init();
	}
	for (uint32_t i = 0; i < len; i++)
	{
		crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFFu];
	}
	return crc;
}

uint32_t dfu_crc32(const uint8_t *data, uint32_t len)
{
	return dfu_crc32_update(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}
