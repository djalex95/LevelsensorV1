/*
 * dfu_common.c
 *
 * Gemeinsame CRC32-Routine für das OTA-Update (App und Bootloader).
 * IEEE/zlib-CRC32 – identisch zur Berechnung in der Handy-App.
 */

#include "dfu_common.h"

uint32_t dfu_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (uint8_t b = 0; b < 8; b++)
		{
			if (crc & 1u)
			{
				crc = (crc >> 1) ^ 0xEDB88320UL;
			}
			else
			{
				crc >>= 1;
			}
		}
	}
	return crc;
}

uint32_t dfu_crc32(const uint8_t *data, uint32_t len)
{
	return dfu_crc32_update(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}
