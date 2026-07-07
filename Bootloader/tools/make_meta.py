#!/usr/bin/env python3
"""
Erzeugt die DFU-Metadaten (Gültigkeits-Marker + Größe + CRC32) aus einer
App-Binärdatei – als .bin UND als .hex mit eingetragener Flash-Adresse.

    python make_meta.py firmware.bin

Ergebnis:
    meta.bin  – 16 Byte, muss bei 0x0801E800 geflasht werden
    meta.hex  – enthält die Adresse 0x0801E800 bereits (direkt flashbar)

Zum Flashen (STM32CubeProgrammer): einfach meta.hex hinzufügen – die Adresse
steckt drin. Im laufenden Betrieb (OTA über die App) schreibt der Bootloader die
Metadaten selbst; dieses Skript ist nur für die Erst-Inbetriebnahme per SWD.
"""

import sys
import zlib
import struct

DFU_META_MAGIC = 0xB007A99C
DFU_META_ADDR = 0x0801E800


def ihex(base, data):
    """Erzeugt einen Intel-HEX-String für 'data' ab absoluter Adresse 'base'."""
    def rec(record):
        chk = (-sum(record)) & 0xFF
        return ":" + bytes(record).hex().upper() + f"{chk:02X}"

    lines = []
    # Extended Linear Address für die oberen 16 Adressbits
    upper = (base >> 16) & 0xFFFF
    lines.append(rec([0x02, 0x00, 0x00, 0x04, (upper >> 8) & 0xFF, upper & 0xFF]))
    # Datensatz (16 Byte passen in einen Record)
    off = base & 0xFFFF
    lines.append(rec([len(data), (off >> 8) & 0xFF, off & 0xFF, 0x00, *data]))
    # End Of File
    lines.append(":00000001FF")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) < 2:
        print("Aufruf: make_meta.py <app.bin>")
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    size = len(data)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    meta = struct.pack("<IIII", DFU_META_MAGIC, size, crc, 0xFFFFFFFF)

    with open("meta.bin", "wb") as f:
        f.write(meta)
    with open("meta.hex", "w") as f:
        f.write(ihex(DFU_META_ADDR, meta))

    print(f"App:  {size} Byte, CRC32 {crc:08X}")
    print(f"meta.bin: 16 Byte  ->  flashen bei 0x{DFU_META_ADDR:08X}")
    print(f"meta.hex: enthält 0x{DFU_META_ADDR:08X} bereits  ->  direkt flashbar")


if __name__ == "__main__":
    main()
