#!/usr/bin/env python3
"""
GUI für die DFU-Metadaten (Gültigkeits-Marker + Größe + CRC32) aus einer
App-Binärdatei. Erzeugt meta.bin und meta.hex (mit eingetragener Flash-Adresse).

Reine Standardbibliothek (Tkinter) – als .exe paketierbar:
    pip install pyinstaller
    pyinstaller --onefile --noconsole make_meta_gui.py
"""

import os
import struct
import zlib
import tkinter as tk
from tkinter import ttk, filedialog

DFU_META_MAGIC = 0xB007A99C
DEFAULT_ADDR = 0x0801E800


def ihex(base, data):
    """Intel-HEX-String für 'data' ab absoluter Adresse 'base'."""
    def rec(record):
        chk = (-sum(record)) & 0xFF
        return ":" + bytes(record).hex().upper() + f"{chk:02X}"

    lines = []
    upper = (base >> 16) & 0xFFFF
    lines.append(rec([0x02, 0x00, 0x00, 0x04, (upper >> 8) & 0xFF, upper & 0xFF]))
    off = base & 0xFFFF
    lines.append(rec([len(data), (off >> 8) & 0xFF, off & 0xFF, 0x00, *data]))
    lines.append(":00000001FF")
    return "\n".join(lines) + "\n"


class App:
    def __init__(self, root):
        self.root = root
        self.bin_path = None
        root.title("DFU Metadaten-Generator")

        pad = {"padx": 10, "pady": 6}
        frm = ttk.Frame(root, padding=12)
        frm.pack(fill="both", expand=True)

        ttk.Label(frm, text="1. Firmware-Datei (.bin) wählen:").grid(
            row=0, column=0, sticky="w", **pad)
        ttk.Button(frm, text="Datei wählen…", command=self.pick).grid(
            row=0, column=1, sticky="e", **pad)

        self.file_lbl = ttk.Label(frm, text="– keine Datei –", foreground="#555")
        self.file_lbl.grid(row=1, column=0, columnspan=2, sticky="w", padx=10)

        ttk.Label(frm, text="Metadaten-Adresse:").grid(
            row=2, column=0, sticky="w", **pad)
        self.addr = ttk.Entry(frm, width=14)
        self.addr.insert(0, f"0x{DEFAULT_ADDR:08X}")
        self.addr.grid(row=2, column=1, sticky="e", **pad)

        self.info = ttk.Label(frm, text="Größe: –    CRC32: –",
                              font=("Segoe UI", 10, "bold"))
        self.info.grid(row=3, column=0, columnspan=2, sticky="w", padx=10, pady=(4, 0))

        self.btn = ttk.Button(frm, text="meta.bin + meta.hex erzeugen",
                              command=self.generate, state="disabled")
        self.btn.grid(row=4, column=0, columnspan=2, sticky="ew", padx=10, pady=10)

        self.log = tk.Text(frm, height=7, width=58, state="disabled",
                           font=("Consolas", 9))
        self.log.grid(row=5, column=0, columnspan=2, sticky="nsew", padx=10, pady=(0, 6))

    def _log(self, msg):
        self.log.config(state="normal")
        self.log.insert("end", msg + "\n")
        self.log.see("end")
        self.log.config(state="disabled")

    def pick(self):
        path = filedialog.askopenfilename(
            title="Firmware-Binärdatei wählen",
            filetypes=[("Binärdatei", "*.bin"), ("Alle Dateien", "*.*")])
        if not path:
            return
        self.bin_path = path
        self.file_lbl.config(text=path)
        try:
            with open(path, "rb") as f:
                data = f.read()
            self.size = len(data)
            self.crc = zlib.crc32(data) & 0xFFFFFFFF
            self.info.config(text=f"Größe: {self.size} Byte    CRC32: {self.crc:08X}")
            self.btn.config(state="normal")
            self._log(f"Gelesen: {os.path.basename(path)} ({self.size} Byte, CRC {self.crc:08X})")
        except OSError as e:
            self._log(f"Fehler beim Lesen: {e}")
            self.btn.config(state="disabled")

    def generate(self):
        if not self.bin_path:
            return
        try:
            addr = int(self.addr.get().strip(), 0)
        except ValueError:
            self._log("Ungültige Adresse (z. B. 0x0801E800).")
            return

        meta = struct.pack("<IIII", DFU_META_MAGIC, self.size, self.crc, 0xFFFFFFFF)
        out_dir = os.path.dirname(self.bin_path)
        bin_out = os.path.join(out_dir, "meta.bin")
        hex_out = os.path.join(out_dir, "meta.hex")
        try:
            with open(bin_out, "wb") as f:
                f.write(meta)
            with open(hex_out, "w") as f:
                f.write(ihex(addr, meta))
        except OSError as e:
            self._log(f"Fehler beim Schreiben: {e}")
            return

        self._log(f"meta.bin  → {bin_out}")
        self._log(f"meta.hex  → {hex_out}  (Adresse 0x{addr:08X})")
        self._log("Fertig. meta.hex direkt flashbar; meta.bin bei der Adresse.")


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
