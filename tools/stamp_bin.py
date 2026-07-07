#!/usr/bin/env python3
"""
Kopiert die gebaute Firmware-.bin mit Versionsnummer im Namen.
Die Version wird aus Core/Inc/version.h (FW_VERSION) gelesen.

Aufruf:
    python tools/stamp_bin.py [Pfad/zur/firmware.bin]

Ohne Argument wird automatisch die neueste .bin in Debug/ bzw. Release/
gesucht. Ergebnis liegt in dist/, z. B. dist/Fuellstandsensor_v1.2.0.bin
"""
import re
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ver_h = root / "Core" / "Inc" / "version.h"

m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', ver_h.read_text(encoding="utf-8"))
if not m:
    sys.exit("FW_VERSION nicht in Core/Inc/version.h gefunden")
version = m.group(1)

if len(sys.argv) > 1:
    src = Path(sys.argv[1])
    if not src.is_file():
        sys.exit(f"Datei nicht gefunden: {src}")
else:
    cands = list((root / "Debug").glob("*.bin")) + list((root / "Release").glob("*.bin"))
    if not cands:
        sys.exit("Keine .bin in Debug/ oder Release/ gefunden – erst in CubeIDE bauen.")
    src = max(cands, key=lambda p: p.stat().st_mtime)

dist = root / "dist"
dist.mkdir(exist_ok=True)
dst = dist / f"Fuellstandsensor_v{version}.bin"
shutil.copy2(src, dst)
print(f"{src.name}  ->  {dst}")
