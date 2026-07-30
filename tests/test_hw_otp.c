/*
 * test_hw_otp.c - Host-Tests fuer die OTP-Slot-Logik (hw_otp.h, Issue #2)
 *
 * Laeuft auf dem PC: hw_otp_find_in() arbeitet auf RAM-Puffern, die das
 * OTP nachbilden. Das echte OTP (jedes Doppelwort nur EINMAL
 * beschreibbar) wird nie beruehrt.
 */
#include <stdio.h>
#include <string.h>
#include "hw_otp.h"

static int fails = 0;
#define CHECK(cond, name) do { \
	if (cond) { printf("ok: %s\n", name); } \
	else { printf("FEHLER: %s\n", name); fails = 1; } \
} while (0)

static void slot_set(hw_otp_slot_t *s, unsigned variant, char rev)
{
	s->magic = HW_OTP_MAGIC;
	s->variant = (uint16_t)variant;
	s->rev = (uint8_t)rev;
	s->reserved = 0;
	s->check = hw_otp_checksum(s);
}

int main(void)
{
	hw_otp_slot_t otp[HW_OTP_SLOTS];
	const hw_otp_slot_t *f;

	/* 1: leeres OTP (0xFF) -> nichts provisioniert */
	memset(otp, 0xFF, sizeof(otp));
	CHECK(hw_otp_find_in(otp) == NULL, "leeres OTP liefert NULL");

	/* 2: ein gueltiger Slot */
	slot_set(&otp[0], 1003, 'A');
	f = hw_otp_find_in(otp);
	CHECK(f != NULL && f->variant == 1003 && f->rev == 'A',
	      "einzelner Slot wird gefunden");

	/* 3: Korrektur -> letzter gueltiger Slot gewinnt */
	slot_set(&otp[1], 1001, 'B');
	f = hw_otp_find_in(otp);
	CHECK(f != NULL && f->variant == 1001 && f->rev == 'B',
	      "letzter gueltiger Slot gewinnt");

	/* 4: Slot mit zerstoertem Pruefbyte wird ignoriert */
	slot_set(&otp[2], 1003, 'C');
	otp[2].check ^= 0xFF;
	f = hw_otp_find_in(otp);
	CHECK(f != NULL && f->variant == 1001 && f->rev == 'B',
	      "Slot mit falschem Pruefbyte wird ignoriert");

	/* 5: Buchstabe ausserhalb A..Z ist ungueltig */
	slot_set(&otp[3], 1003, '1');
	f = hw_otp_find_in(otp);
	CHECK(f != NULL && f->variant == 1001 && f->rev == 'B',
	      "Slot mit ungueltigem Buchstaben wird ignoriert");

	/* 6: auch der letzte Listenplatz zaehlt */
	slot_set(&otp[HW_OTP_SLOTS - 1], 1003, 'D');
	f = hw_otp_find_in(otp);
	CHECK(f != NULL && f->variant == 1003 && f->rev == 'D',
	      "letzter Listenplatz wird beruecksichtigt");

	/* 7: Layout-Vertrag mit Bootloader und Provisionierungs-Tool */
	CHECK(sizeof(hw_otp_slot_t) == 8, "Slot ist exakt 8 Byte gross");

	if (fails) { puts("Es sind hw_otp-Tests fehlgeschlagen."); return 1; }
	puts("Alle hw_otp-Tests bestanden.");
	return 0;
}
