/*
 * dfu_common.h
 *
 * Gemeinsame Definitionen für das OTA-Firmware-Update (App und Bootloader).
 * Siehe Bootloader/DESIGN.md für den Gesamtentwurf.
 */

#ifndef INC_DFU_COMMON_H_
#define INC_DFU_COMMON_H_

#include <stdint.h>

/* ---- Flash-Layout (siehe DESIGN.md) ---- */
#define DFU_BOOT_ADDR    0x08000000UL              /* Bootloader (32 KB)        */
#define DFU_APP_ADDR     0x08008000UL              /* Anwendungsstart           */
#define DFU_APP_MAX      (90UL * 1024UL)           /* max. Anwendungsgröße      */
#define DFU_META_ADDR    0x0801E800UL              /* Metadaten-Seite (Seite 61)*/

/* ---- Enter-DFU-Anforderung (RAM, übersteht Warmstart) ----
 * Liegt in den reservierten obersten 32 Byte des RAM. Die App schreibt das
 * Magic und löst einen Reset aus; der Bootloader liest es beim Start. */
#define DFU_REQ_ADDR     ((volatile uint32_t *)0x20023FE0UL)
#define DFU_REQ_MAGIC    0xDF00B007UL

/* ---- Metadaten der gültigen App (in DFU_META_ADDR) ---- */
#define DFU_META_MAGIC   0xB007A99CUL              /* Marker „App gültig"       */

typedef struct {
	uint32_t magic;       /* DFU_META_MAGIC, wenn eine gültige App vorliegt   */
	uint32_t app_size;    /* Größe der App in Byte                            */
	uint32_t app_crc32;   /* CRC32 über app_size Byte ab DFU_APP_ADDR         */
	uint32_t reserved;    /* auf 8-Byte-Doppelwort ausgerichtet               */
} dfu_meta_t;

/* ---- DFU-Transferprotokoll (Bootloader ↔ Handy) ----
 * 4-Byte-ASCII-Kennungen am Anfang jeder Nachricht. */
#define DFU_TAG_START    "DFUS"   /* + Größe(4 LE) + CRC32(4 LE)              */
#define DFU_TAG_DATA     "DFUD"   /* + Offset(4 LE) + Nutzdaten               */
#define DFU_TAG_END      "DFUE"   /* Abschluss, Verifikation                  */

/* ---- CRC32 (IEEE/zlib: Poly 0xEDB88320, Init 0xFFFFFFFF, XorOut 0xFFFFFFFF) ----
 * Identisch in App, Bootloader und Handy-App, damit die Prüfsummen passen. */
uint32_t dfu_crc32(const uint8_t *data, uint32_t len);

/* Fortsetzbares CRC32 (für blockweises Rechnen über den Flash):
 *   uint32_t c = 0xFFFFFFFF;
 *   c = dfu_crc32_update(c, buf, n);  // je Block
 *   c ^= 0xFFFFFFFF;                  // am Ende
 */
uint32_t dfu_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

#endif /* INC_DFU_COMMON_H_ */
