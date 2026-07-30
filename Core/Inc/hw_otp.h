/*
 * hw_otp.h - Hardware-Kennung im OTP-Bereich (Issue #2)
 *
 * Die Hardware-Variante (z. B. 1003) und der Platinen-Buchstabe ("A")
 * werden bei der Produktion in den OTP-Bereich des STM32G0B1 geschrieben
 * und sind damit eine Eigenschaft des Geraets statt einer Behauptung der
 * Firmware. Gemeinsame Definition fuer App-Firmware und Bootloader; das
 * Provisionierungs-Tool (LevelSense_Bootloader, tools/provision/) haelt
 * eine dokumentierte Kopie dieser Konstanten.
 *
 * OTP: 1 KB ab 0x1FFF7000, nur 64-Bit-Doppelworte, jedes Doppelwort
 * wegen der Flash-ECC nur EINMAL beschreibbar - auch das Nachschreiben
 * desselben Werts ist verboten. Deshalb eine Slot-Liste statt eines
 * Einzelwerts: eine Korrektur ist ein neuer Slot dahinter, es gilt
 * immer der letzte gueltige Eintrag.
 */
#ifndef INC_HW_OTP_H_
#define INC_HW_OTP_H_

#include <stdint.h>
#include <stddef.h>

#define HW_OTP_ADDR    0x1FFF7000UL   /* OTP-Basis STM32G0B1 */
#define HW_OTP_SLOTS   16             /* 16 x 8 Byte = 128 Byte reserviert */
#define HW_OTP_MAGIC   0x4857U        /* "HW" */

typedef struct {
	uint16_t magic;     /* HW_OTP_MAGIC */
	uint16_t variant;   /* 1001, 1003, ... */
	uint8_t  rev;       /* Platinen-Buchstabe, ASCII 'A'..'Z' */
	uint8_t  check;     /* hw_otp_checksum() */
	uint16_t reserved;  /* beim Schreiben 0x0000 */
} hw_otp_slot_t;

/* Pruefbyte: Komplement des XOR aller Nutzbytes. Ein leerer Slot
 * (0xFF...) und zufaellige Bitmuster fallen damit durch. */
static inline uint8_t hw_otp_checksum(const hw_otp_slot_t *s)
{
	return (uint8_t)~((uint8_t)(s->magic & 0xFF) ^ (uint8_t)(s->magic >> 8)
	                  ^ (uint8_t)(s->variant & 0xFF) ^ (uint8_t)(s->variant >> 8)
	                  ^ s->rev);
}

static inline int hw_otp_slot_valid(const hw_otp_slot_t *s)
{
	return s->magic == HW_OTP_MAGIC
	       && s->rev >= 'A' && s->rev <= 'Z'
	       && s->check == hw_otp_checksum(s);
}

/* Letzten gueltigen Slot unter den HW_OTP_SLOTS Eintraegen ab [base]
 * suchen. Basis als Parameter, damit Host-Tests mit RAM-Puffern
 * arbeiten koennen. */
static inline const hw_otp_slot_t *hw_otp_find_in(const void *base)
{
	const hw_otp_slot_t *arr = (const hw_otp_slot_t *)base;
	const hw_otp_slot_t *found = NULL;
	for (int i = 0; i < HW_OTP_SLOTS; i++)
	{
		if (hw_otp_slot_valid(&arr[i]))
		{
			found = &arr[i];
		}
	}
	return found;
}

/* Bequemer Zugriff auf das echte OTP. NULL = nichts provisioniert. */
static inline const hw_otp_slot_t *hw_otp_find(void)
{
	return hw_otp_find_in((const void *)HW_OTP_ADDR);
}

#endif /* INC_HW_OTP_H_ */
