/*
 * boot_dfu.c  – siehe boot_dfu.h
 */

#include "boot_dfu.h"
#include "dfu_common.h"
#include <string.h>
#include <stdio.h>

static const dfu_flash_ops_t *fops;

static uint32_t exp_size;   /* erwartete App-Größe (aus DFUS)  */
static uint32_t exp_crc;    /* erwartete CRC32 (aus DFUS)      */
static uint32_t recv;       /* bisher empfangene Byte          */
static uint32_t flash_pos;  /* bisher in den Flash geschriebene Byte (Vielfaches 8) */
static uint8_t  stage[8];   /* Sammelpuffer bis Doppelwort voll */
static uint8_t  stage_n;
static uint8_t  active;     /* 1 = Übertragung läuft            */

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void dfu_init(const dfu_flash_ops_t *ops)
{
	fops = ops;
	exp_size = exp_crc = recv = flash_pos = 0;
	stage_n = 0;
	active = 0;
}

/* Bytes in den Flash strömen: sammelt zu 8-Byte-Doppelworten und schreibt. */
static int feed(const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
	{
		stage[stage_n++] = p[i];
		if (stage_n == 8)
		{
			if (fops->write(flash_pos, stage, 8) != 0)
			{
				return -1;
			}
			flash_pos += 8;
			stage_n = 0;
		}
	}
	return 0;
}

/* Rest im Sammelpuffer mit 0xFF auffüllen und schreiben. */
static int flush_pad(void)
{
	if (stage_n == 0)
	{
		return 0;
	}
	while (stage_n < 8)
	{
		stage[stage_n++] = 0xFF;
	}
	int r = fops->write(flash_pos, stage, 8);
	flash_pos += 8;
	stage_n = 0;
	return r;
}

uint16_t dfu_on_data(const uint8_t *d, uint16_t len, char *resp, uint8_t *do_reset)
{
	*do_reset = 0;
	resp[0] = '\0';

	if (len >= 4 && memcmp(d, DFU_TAG_START, 4) == 0)
	{
		if (len < 12)
		{
			strcpy(resp, "DFUS ERR\n");
		}
		else
		{
			uint32_t size = rd32(d + 4);
			uint32_t crc = rd32(d + 8);
			if (size == 0 || size > DFU_APP_MAX)
			{
				strcpy(resp, "DFUS ERR size\n");
			}
			else if (fops->erase_app() != 0)
			{
				strcpy(resp, "DFUS ERR erase\n");
			}
			else
			{
				exp_size = size;
				exp_crc = crc;
				recv = 0;
				flash_pos = 0;
				stage_n = 0;
				active = 1;
				strcpy(resp, "DFUS OK\n");
			}
		}
	}
	else if (len >= 4 && memcmp(d, DFU_TAG_DATA, 4) == 0)
	{
		if (!active || len < 8)
		{
			strcpy(resp, "DFUD ERR\n");
		}
		else
		{
			uint32_t off = rd32(d + 4);
			const uint8_t *payload = d + 8;
			uint32_t plen = (uint32_t)(len - 8);

			if (off != recv)
			{
				/* Sequenzfehler: erwartete Position zurückmelden (Retransmit) */
				snprintf(resp, 40, "DFUD ERR seq %lu\n", (unsigned long)recv);
			}
			else
			{
				if (recv + plen > exp_size)
				{
					plen = exp_size - recv;   /* letztes Paket ggf. kürzen */
				}
				if (feed(payload, plen) != 0)
				{
					active = 0;
					strcpy(resp, "DFUD ERR write\n");
				}
				else
				{
					recv += plen;
					snprintf(resp, 40, "DFUD %lu OK\n", (unsigned long)recv);
				}
			}
		}
	}
	else if (len >= 4 && memcmp(d, DFU_TAG_END, 4) == 0)
	{
		if (!active)
		{
			strcpy(resp, "DFUE ERR\n");
		}
		else if (recv != exp_size)
		{
			active = 0;
			strcpy(resp, "DFUE ERR size\n");
		}
		else if (flush_pad() != 0)
		{
			active = 0;
			strcpy(resp, "DFUE ERR write\n");
		}
		else if (fops->crc(exp_size) != exp_crc)
		{
			active = 0;
			strcpy(resp, "DFUE ERR crc\n");
		}
		else if (fops->commit_meta(exp_size, exp_crc) != 0)
		{
			active = 0;
			strcpy(resp, "DFUE ERR meta\n");
		}
		else
		{
			active = 0;
			*do_reset = 1;
			strcpy(resp, "DFUE OK\n");
		}
	}
	else
	{
		strcpy(resp, "ERR ?\n");
	}

	return (uint16_t)strlen(resp);
}
