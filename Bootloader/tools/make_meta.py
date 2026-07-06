#!/usr/bin/env python3
"""
Erzeugt die DFU-Metadaten (Gültigkeits-Marker + Größe + CRC32) aus einer
App-Binärdatei. Zum Flashen bei der Erst-Inbetriebnahme per SWD:

    python make_meta.py firmware.bin meta.bin

Anschließend per STM32CubeProgrammer flashen:
    Bootloader   -> 0x08000000
    App (.bin)   -> 0x08008000
    meta.bin     -> 0x0801E800

Im laufenden Betrieb (OTA über die App) schreibt der Bootloader die Metadaten
selbst – dieses Skript ist nur für den ersten Start per Debugger nötig.
"""

import sys
import zlib
import struct

DFU_META_MAGIC = 0xB007A99C

def main():
    if len(sys.argv) < 2:
        print("Aufruf: make_meta.py <app.bin> [meta.bin]")
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        data = f.read()

    size = len(data)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    meta = struct.pack("<IIII", DFU_META_MAGIC, size, crc, 0xFFFFFFFF)

    out = sys.argv[2] if len(sys.argv) > 2 else "meta.bin"
    with open(out, "wb") as f:
        f.write(meta)

    print(f"App:  {size} Byte, CRC32 {crc:08X}")
    print(f"{out}: 16 Byte geschrieben  ->  flashen bei 0x0801E800")

if __name__ == "__main__":
    main()
