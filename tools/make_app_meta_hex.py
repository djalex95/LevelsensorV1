#!/usr/bin/env python3
"""
make_app_meta_hex.py <app.bin> <output.hex>

Erzeugt eine KOMBINIERTE Intel-HEX-Datei aus dem App-Binary:
  - App-Bytes ab 0x08008000
  - DFU-Metadaten (16 Byte) ab 0x0801E800 (Magic + Groesse + CRC32)

Diese .hex laesst sich mit STM32CubeProgrammer / CubeIDE in EINEM Schritt
flashen (App + Meta zusammen), sodass die Metadaten IMMER zur geflashten App
passen - das haendische Erzeugen/Flashen der meta.hex entfaellt. Gedacht als
CubeIDE-Post-Build-Schritt (siehe tools/README_build.md).

Der Bootloader (0x08000000) ist bewusst NICHT enthalten: er liegt bereits auf
dem Chip und wird selten neu geflasht. Fuer eine Erst-Inbetriebnahme zusaetzlich
den Bootloader flashen (siehe BootloaderCube).

WICHTIG: Die Konstanten muessen mit dem Bootloader uebereinstimmen
(BootloaderCube: Core/Inc/dfu_common.h bzw. tools/make_meta.py).
"""

import sys
import zlib
import struct

APP_ADDR       = 0x08008000   # Anwendungsstart (hinter dem Bootloader)
META_ADDR      = 0x0801E800   # Metadaten-Seite (Seite 61)
DFU_META_MAGIC = 0xB007A99C   # Marker "App gueltig"


def ihex_records(base, data):
    """Liefert Intel-HEX-Zeilen fuer 'data' ab absoluter Adresse 'base'.
    Setzt Extended-Linear-Address-Records (Typ 0x04) beim Wechsel der oberen
    16 Adressbits und bricht Datensaetze an der 64K-Grenze sauber um."""
    out = []

    def rec(fields):
        chk = (-sum(fields)) & 0xFF
        return ":" + bytes(fields).hex().upper() + f"{chk:02X}"

    cur_upper = None
    pos, n = 0, len(data)
    while pos < n:
        addr = base + pos
        upper = (addr >> 16) & 0xFFFF
        if upper != cur_upper:
            out.append(rec([0x02, 0x00, 0x00, 0x04,
                            (upper >> 8) & 0xFF, upper & 0xFF]))
            cur_upper = upper
        off = addr & 0xFFFF
        chunk = min(16, n - pos, 0x10000 - off)   # nicht ueber die 64K-Grenze
        out.append(rec([chunk, (off >> 8) & 0xFF, off & 0xFF, 0x00,
                        *data[pos:pos + chunk]]))
        pos += chunk
    return out


def main():
    if len(sys.argv) != 3:
        print("Aufruf: make_app_meta_hex.py <app.bin> <output.hex>")
        sys.exit(1)

    with open(sys.argv[1], "rb") as f:
        app = f.read()

    size = len(app)
    if APP_ADDR + size > META_ADDR:
        print(f"FEHLER: App ist {size} Byte gross und wuerde die Meta-Seite "
              f"(0x{META_ADDR:08X}) ueberschreiben.")
        sys.exit(1)

    crc = zlib.crc32(app) & 0xFFFFFFFF
    meta = struct.pack("<IIII", DFU_META_MAGIC, size, crc, 0xFFFFFFFF)

    # 1) Kombinierte .hex (App + Meta) - fuer CubeProgrammer in einem Schritt
    lines = ihex_records(APP_ADDR, app)
    lines += ihex_records(META_ADDR, meta)
    lines.append(":00000001FF")   # End Of File
    with open(sys.argv[2], "w") as f:
        f.write("\n".join(lines) + "\n")

    # 2) Standalone meta.hex daneben - fuer den Debug-Launch als Zusatz-Image
    import os
    meta_path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])),
                             "meta.hex")
    meta_lines = ihex_records(META_ADDR, meta)
    meta_lines.append(":00000001FF")
    with open(meta_path, "w") as f:
        f.write("\n".join(meta_lines) + "\n")

    print(f"App {size} Byte, CRC32 {crc:08X}")
    print(f"  App  @ 0x{APP_ADDR:08X}")
    print(f"  Meta @ 0x{META_ADDR:08X}")
    print(f"  -> {sys.argv[2]}  (App + Meta kombiniert)")
    print(f"  -> {meta_path}  (nur Meta)")


if __name__ == "__main__":
    main()
