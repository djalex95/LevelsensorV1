#!/usr/bin/env python3
"""
Kopiert die gebaute Firmware-.bin mit Versionsnummer und Variante im Namen.

Die Version kommt aus Core/Inc/version.h (FW_VERSION), die Variante aus dem
Build-Ordner selbst: CubeIDE schreibt die Uebersetzungsbefehle in die
erzeugten subdir.mk, und dort steht das -DHW_VARIANT=<nnnn>, mit dem
tatsaechlich gebaut wurde. Der Ordnername waere unzuverlaessig, weil die
Konfiguration "Release" keinen Variantensuffix traegt.

Aufruf:
    python tools/stamp_bin.py [Pfad/zur/firmware.bin] [Variante]

Ohne Argument wird die neueste .bin in Debug*/ bzw. Release*/ gesucht.
Ergebnis liegt in dist/, z. B. dist/Fuellstandsensor_v1.2.11_hwv1000.bin -
derselbe Name wie das zugehoerige Release-Asset.
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


def firmware_bins(build_dir):
    """Bauergebnisse eines Build-Ordners. meta.bin ist die 16 Byte grosse
    Meta aus dem Post-Build-Schritt und nicht die Firmware."""
    return [p for p in build_dir.glob("*.bin") if p.name != "meta.bin"]


def variant_of(build_dir):
    """Variante aus den erzeugten subdir.mk lesen. Ergibt None, wenn sich
    keine oder mehr als eine finden laesst."""
    found = set()
    for mk in build_dir.rglob("subdir.mk"):
        text = mk.read_text(encoding="utf-8", errors="replace")
        found.update(re.findall(r"HW_VARIANT=(\d+)", text))
    return found.pop() if len(found) == 1 else None


def rel(p):
    try:
        return p.relative_to(root)
    except ValueError:
        return p


if len(sys.argv) > 1:
    src = Path(sys.argv[1])
    if not src.is_file():
        sys.exit(f"Datei nicht gefunden: {src}")
else:
    cands = []
    for d in sorted(root.glob("Debug*")) + sorted(root.glob("Release*")):
        if d.is_dir():
            cands += firmware_bins(d)
    if not cands:
        sys.exit("Keine .bin in Debug*/ oder Release*/ gefunden - erst in CubeIDE bauen.")
    src = max(cands, key=lambda p: p.stat().st_mtime)

variant = sys.argv[2] if len(sys.argv) > 2 else variant_of(src.parent)
if not variant:
    sys.exit(
        f"Variante zu {rel(src)} nicht ermittelbar - kein eindeutiges "
        f"HW_VARIANT in {rel(src.parent)}/**/subdir.mk. Variante als zweites "
        f"Argument angeben."
    )

dist = root / "dist"
dist.mkdir(exist_ok=True)
dst = dist / f"Fuellstandsensor_v{version}_hwv{variant}.bin"
shutil.copy2(src, dst)
print(f"{rel(src)}  ->  {rel(dst)}")
