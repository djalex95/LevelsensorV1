#!/usr/bin/env python3
"""
GUI für den NMEA2000-Füllstandsensor über PEAK PCAN-USB.

Zeigt Füllstand (Balken + Zahlenwert), Fluidtyp, Kapazität, Quelladresse,
einen Verlaufsgraphen (Füllstand über Zeit) und ein Log (Address Claims,
Product Info, Fehler).

Voraussetzungen:
  - PEAK-Treiber / PCAN-Basic installiert
  - pip install python-can
  - nmea2000_reader.py im selben Ordner (Decoder wird wiederverwendet)

Aufruf:  python nmea2000_gui.py
"""

import os
import queue
import struct
import sys
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import ttk


def resource_path(name):
    """Pfad zu einer mitgelieferten Datei – funktioniert im Skript und in der
    von PyInstaller entpackten .exe (sys._MEIPASS)."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, name)

import can

from nmea2000_reader import (
    BITRATE, FLUID_TYPES, PC_SOURCE_ADDR, PROP_PGN, SENSOR_ADDR,
    FastPacketAssembler, build_calib_max, build_calib_reset,
    build_gf_command_127505, build_lin_table_read, build_lin_table_write,
    decode_address_claim, decode_can_id, decode_gf, decode_product_info,
    decode_prop, encode_can_id, send_fast_packet,
)
from tkinter import messagebox

HISTORY_SECONDS = 300      # Zeitfenster des Verlaufsgraphen
POLL_MS = 100              # GUI-Poll-Intervall für die RX-Queue


def parse_fluid_level(data: bytes):
    """PGN-127505-Payload -> dict oder None."""
    if len(data) < 7:
        return None
    raw_level = struct.unpack_from("<h", data, 1)[0]
    raw_cap = struct.unpack_from("<I", data, 3)[0]
    return {
        "instance": data[0] & 0x0F,
        "fluid": (data[0] >> 4) & 0x0F,
        "level": None if raw_level == 0x7FFE else raw_level / 250.0,
        "cap": None if raw_cap == 0xFFFFFFFE else raw_cap * 0.1,
    }


class RxThread(threading.Thread):
    """Liest den CAN-Bus und legt dekodierte Ereignisse in die Queue."""

    def __init__(self, bus: can.Bus, out: queue.Queue):
        super().__init__(daemon=True)
        self.bus = bus
        self.out = out
        self.stop_flag = threading.Event()
        self.fp = FastPacketAssembler()

    def run(self):
        while not self.stop_flag.is_set():
            try:
                msg = self.bus.recv(timeout=0.2)
            except can.CanError as e:
                self.out.put(("log", f"CAN-Fehler: {e}"))
                time.sleep(0.5)
                continue
            if msg is None or not msg.is_extended_id:
                continue
            _, pgn, _, src = decode_can_id(msg.arbitration_id)
            data = bytes(msg.data)

            if pgn == 127505:
                lvl = parse_fluid_level(data)
                if lvl:
                    lvl["src"] = src
                    self.out.put(("level", lvl))
            elif pgn == 130312 and len(data) >= 5:
                raw = struct.unpack_from("<H", data, 3)[0]
                if raw not in (0xFFFF, 0xFFFE):
                    self.out.put(("temp", raw / 100.0 - 273.15))
            elif pgn == 60928:
                txt = decode_address_claim(data, src)
                if txt:
                    self.out.put(("log", txt))
            elif pgn in (126996, 126998, 126208, PROP_PGN):
                full = self.fp.feed(pgn, src, data)
                if full is not None:
                    if pgn == 126996:
                        self.out.put(("log", decode_product_info(full, src)))
                    elif pgn == 126208:
                        txt = decode_gf(full, src)
                        if txt:
                            self.out.put(("log", txt))
                    elif pgn == PROP_PGN:
                        r = decode_prop(full, src)
                        if r:
                            self.out.put(r)   # ("lin_ack", text) oder ("lin_table", werte)
                    else:
                        s = full.split(b"\xff")[0].decode("ascii", "replace")
                        self.out.put(("log", f"[0x{src:02X}] Device Info: '{s}'"))


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("NMEA2000 Füllstandsensor")
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.bus = None
        self.rx = None
        self.q = queue.Queue()
        self.history = deque()          # (timestamp, level_percent)
        self.last_rx = None
        self.sensor_addr = SENSOR_ADDR  # wird dynamisch aktualisiert (Adress-Arbitrierung!)
        self.tank_cap = None            # Kapazität aus PGN 127505 (Liter)

        # ---- Verbindungszeile ----
        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")
        ttk.Label(top, text="Kanal:").pack(side="left")
        self.channel = ttk.Combobox(top, width=14, state="readonly",
                                    values=[f"PCAN_USBBUS{i}" for i in range(1, 5)])
        self.channel.set("PCAN_USBBUS1")
        self.channel.pack(side="left", padx=(4, 8))
        self.btn_conn = ttk.Button(top, text="Verbinden", command=self.toggle_connect)
        self.btn_conn.pack(side="left")
        self.btn_req = ttk.Button(top, text="Product Info anfragen",
                                  command=self.request_info, state="disabled")
        self.btn_req.pack(side="left", padx=8)
        self.btn_calib = ttk.Button(top, text="Kalibrierung…",
                                    command=self.open_calib_window, state="disabled")
        self.btn_calib.pack(side="left")
        self.status = ttk.Label(top, text="getrennt", foreground="red")
        self.status.pack(side="right")

        # ---- Füllstandsanzeige ----
        mid = ttk.Frame(root, padding=8)
        mid.pack(fill="x")
        self.lbl_level = ttk.Label(mid, text="-- %", font=("Segoe UI", 32, "bold"))
        self.lbl_level.pack()
        self.bar = ttk.Progressbar(mid, maximum=100, length=560)
        self.bar.pack(pady=4)
        self.lbl_detail = ttk.Label(mid, text="Fluid: –   Kapazität: –   Instanz: –   Quelle: –")
        self.lbl_detail.pack()
        self.lbl_temp = ttk.Label(mid, text="Temperatur: –", font=("Segoe UI", 12))
        self.lbl_temp.pack()

        # ---- Konfiguration (PGN 126208 Command auf 127505) ----
        cfg = ttk.LabelFrame(root, text="Sensor konfigurieren", padding=6)
        cfg.pack(fill="x", padx=8, pady=(4, 0))
        ttk.Label(cfg, text="Fluidtyp:").pack(side="left")
        self.cfg_fluid = ttk.Combobox(cfg, width=12, state="readonly",
                                      values=[f"{k}: {v}" for k, v in sorted(FLUID_TYPES.items())])
        self.cfg_fluid.set("1: Water")
        self.cfg_fluid.pack(side="left", padx=(4, 10))
        ttk.Label(cfg, text="Kapazität (L):").pack(side="left")
        self.cfg_cap = ttk.Entry(cfg, width=6)
        self.cfg_cap.insert(0, "100")
        self.cfg_cap.pack(side="left", padx=(4, 10))
        ttk.Label(cfg, text="Instanz:").pack(side="left")
        self.cfg_inst = ttk.Entry(cfg, width=4)
        self.cfg_inst.insert(0, "0")
        self.cfg_inst.pack(side="left", padx=(4, 10))
        self.btn_cfg = ttk.Button(cfg, text="Senden", command=self.send_config,
                                  state="disabled")
        self.btn_cfg.pack(side="left")

        # ---- Tankgeometrie: 11 Stützstellen (Volumen-% bei 0..100 % Füllhöhe) ----
        lin = ttk.LabelFrame(root, text="Tankgeometrie – Volumen-% je Füllhöhe", padding=6)
        lin.pack(fill="x", padx=8, pady=(4, 0))
        self.lin_entries = []
        self.lin_liters = []
        for i in range(11):
            col = ttk.Frame(lin)
            col.pack(side="left", padx=1)
            ttk.Label(col, text=f"{i*10}", font=("Segoe UI", 7)).pack()
            e = ttk.Entry(col, width=4, justify="center")
            e.insert(0, str(i * 10))
            e.pack()
            e.bind("<KeyRelease>", lambda _ev: self.update_lin_liters())
            self.lin_entries.append(e)
            lbl = ttk.Label(col, text="–", font=("Segoe UI", 7), foreground="#666")
            lbl.pack()
            self.lin_liters.append(lbl)
        btns = ttk.Frame(lin)
        btns.pack(side="left", padx=(10, 0))
        self.btn_lin_read = ttk.Button(btns, text="Lesen", width=8,
                                       command=self.read_lin_table, state="disabled")
        self.btn_lin_read.pack(pady=1)
        self.btn_lin_write = ttk.Button(btns, text="Senden", width=8,
                                        command=self.write_lin_table, state="disabled")
        self.btn_lin_write.pack(pady=1)

        # ---- Verlaufsgraph ----
        ttk.Label(root, text=f"Verlauf (letzte {HISTORY_SECONDS // 60} min)",
                  padding=(8, 4, 0, 0)).pack(anchor="w")
        self.canvas = tk.Canvas(root, height=180, bg="white",
                                highlightthickness=1, highlightbackground="#999")
        self.canvas.pack(fill="x", padx=8)

        # ---- Log ----
        ttk.Label(root, text="Log", padding=(8, 4, 0, 0)).pack(anchor="w")
        logf = ttk.Frame(root)
        logf.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self.log = tk.Text(logf, height=6, state="disabled", font=("Consolas", 9))
        sb = ttk.Scrollbar(logf, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log.pack(side="left", fill="both", expand=True)

        # Fenster an den Inhalt anpassen und nicht kleiner zulassen,
        # damit immer alles sichtbar ist.
        root.update_idletasks()
        root.minsize(root.winfo_reqwidth(), root.winfo_reqheight())

        root.after(POLL_MS, self.poll)
        root.after(1000, self.watchdog)

    # ---------------- Verbindung ----------------

    def toggle_connect(self):
        if self.bus is None:
            try:
                self.bus = can.Bus(interface="pcan", channel=self.channel.get(),
                                   bitrate=BITRATE)
            except Exception as e:
                self.log_line(f"Verbindung fehlgeschlagen: {e}")
                self.bus = None
                return
            self.rx = RxThread(self.bus, self.q)
            self.rx.start()
            self.btn_conn.config(text="Trennen")
            self.btn_req.config(state="normal")
            self.btn_cfg.config(state="normal")
            self.btn_calib.config(state="normal")
            self.btn_lin_read.config(state="normal")
            self.btn_lin_write.config(state="normal")
            self.status.config(text=f"verbunden ({self.channel.get()})", foreground="green")
            self.log_line(f"Verbunden: {self.channel.get()} @ {BITRATE // 1000} kbit/s")
        else:
            self.disconnect()

    def disconnect(self):
        if self.rx:
            self.rx.stop_flag.set()
            self.rx.join(timeout=1)
            self.rx = None
        if self.bus:
            self.bus.shutdown()
            self.bus = None
        self.btn_conn.config(text="Verbinden")
        self.btn_req.config(state="disabled")
        self.btn_cfg.config(state="disabled")
        self.btn_calib.config(state="disabled")
        self.btn_lin_read.config(state="disabled")
        self.btn_lin_write.config(state="disabled")
        self.status.config(text="getrennt", foreground="red")
        self.log_line("Getrennt.")

    def request_info(self):
        if not self.bus:
            return
        for pgn, dest in ((60928, 0xFF), (126996, self.sensor_addr)):
            can_id = encode_can_id(6, 59904, dest, PC_SOURCE_ADDR)
            data = bytes([pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF])
            try:
                self.bus.send(can.Message(arbitration_id=can_id, data=data,
                                          is_extended_id=True))
                self.log_line(f">> ISO Request PGN {pgn} an 0x{dest:02X}")
            except can.CanError as e:
                self.log_line(f"Senden fehlgeschlagen: {e}")

    def send_config(self):
        """Sendet die Konfiguration als Command Group Function (PGN 126208)."""
        if not self.bus:
            return
        try:
            fluid = int(self.cfg_fluid.get().split(":")[0])
            cap = float(self.cfg_cap.get().replace(",", "."))
            inst = int(self.cfg_inst.get())
            if not (0 <= inst <= 15):
                raise ValueError("Instanz muss 0..15 sein")
            if not (0 < cap <= 255):
                raise ValueError("Kapazität muss 1..255 L sein")
        except ValueError as e:
            self.log_line(f"Ungültige Eingabe: {e}")
            return
        payload = build_gf_command_127505(instance=inst, fluid_type=fluid,
                                          capacity_liters=cap)
        try:
            send_fast_packet(self.bus, 126208, self.sensor_addr, PC_SOURCE_ADDR, payload)
            self.log_line(f">> Konfiguration an 0x{self.sensor_addr:02X}: "
                          f"Typ={FLUID_TYPES.get(fluid, fluid)}, {cap:.0f} L, Instanz {inst}"
                          f" – warte auf Acknowledge …")
        except can.CanError as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    # ---------------- Kalibrier-Fenster ----------------

    def open_calib_window(self):
        if getattr(self, "_calib_win", None) and self._calib_win.winfo_exists():
            self._calib_win.lift()
            return
        win = tk.Toplevel(self.root)
        self._calib_win = win
        win.title("Kalibrierung")
        win.transient(self.root)

        # --- Maximalwert (100 %) ---
        f1 = ttk.LabelFrame(win, text="Maximalwert (100 %)", padding=8)
        f1.pack(fill="x", padx=8, pady=(8, 4))
        ttk.Label(f1, wraplength=400, justify="left",
                  text="Tank vollständig füllen, kurz warten bis der Wert stabil ist, "
                       "dann »Als 100 % setzen«. Der aktuelle Druck wird als voller "
                       "Tank gespeichert.").pack(anchor="w", pady=(0, 6))
        row = ttk.Frame(f1)
        row.pack(fill="x")
        ttk.Button(row, text="Als 100 % setzen",
                   command=lambda: self._send_prop(build_calib_max(),
                                                   "100%-Kalibrierung gesendet")).pack(side="left")
        ttk.Button(row, text="Zurücksetzen",
                   command=lambda: self._send_prop(build_calib_reset(),
                                                   "Reset der Kalibrierung gesendet")).pack(side="left", padx=6)
        self.calib_level_lbl = ttk.Label(f1, text="aktueller Füllstand: –", foreground="#0066cc")
        self.calib_level_lbl.pack(anchor="w", pady=(6, 0))

        # --- Tankform: Software gibt Füllmenge vor, Nutzer trägt Füllstand ein ---
        f2 = ttk.LabelFrame(win, text="Tankform – Schritt für Schritt einmessen", padding=8)
        f2.pack(fill="both", expand=True, padx=8, pady=4)
        cap_txt = f"{self.tank_cap:.0f} L" if self.tank_cap else "unbekannt (Kapazität erst setzen)"
        self.calib_cap_lbl = ttk.Label(f2, text=f"Tankkapazität: {cap_txt}")
        self.calib_cap_lbl.pack(anchor="w")
        ttk.Label(f2, wraplength=430, justify="left", foreground="#555",
                  text="Tank leeren, dann Zeile für Zeile die angegebene Menge einfüllen. "
                       "Nach jedem Schritt kurz warten und den angezeigten Füllstand "
                       "eintragen – oder mit »Übernehmen« den aktuellen Wert holen.").pack(
                  anchor="w", pady=(0, 4))

        prep = ttk.Frame(f2)
        prep.pack(fill="x", pady=(0, 4))
        ttk.Button(prep, text="Messung vorbereiten",
                   command=self.prepare_lin_measure).pack(side="left")
        ttk.Label(prep, text="setzt die Kennlinie auf linear – Sensor zeigt dann rohe Höhe",
                  foreground="#888", font=("Segoe UI", 7)).pack(side="left", padx=(6, 0))

        hdr = ttk.Frame(f2)
        hdr.pack(fill="x", pady=(4, 0))
        ttk.Label(hdr, text="einfüllen", width=18, font=("Segoe UI", 8, "bold")).pack(side="left")
        ttk.Label(hdr, text="→ Vol.", width=6, font=("Segoe UI", 8, "bold")).pack(side="left")
        ttk.Label(hdr, text="Füllstand [%]", font=("Segoe UI", 8, "bold")).pack(side="left")

        self.calib_height_entries = []
        self.calib_fill_labels = []
        for i in range(11):
            r = ttk.Frame(f2)
            r.pack(fill="x", pady=1)
            fl = ttk.Label(r, text=self._fill_step_text(i), width=18)
            fl.pack(side="left")
            self.calib_fill_labels.append(fl)
            ttk.Label(r, text=f"{i*10} %", width=6).pack(side="left")
            e = ttk.Entry(r, width=7, justify="right")
            e.pack(side="left")
            self.calib_height_entries.append(e)
            ttk.Button(r, text="Übernehmen", width=11,
                       command=lambda idx=i: self.capture_level(idx)).pack(side="left", padx=(4, 0))

        ttk.Button(f2, text="Tabelle berechnen & senden",
                   command=self.send_calib_table).pack(anchor="e", pady=(8, 0))

        # Fenster an den Inhalt anpassen, damit alles ohne Ziehen sichtbar ist
        win.update_idletasks()
        win.minsize(win.winfo_reqwidth(), win.winfo_reqheight())

    def _fill_step_text(self, i):
        """Text der Füll-Vorgabe je Zeile (abhängig von der Tankkapazität)."""
        if not self.tank_cap:
            return f"{i*10} % Volumen"
        if i == 0:
            return "Tank leeren"
        step = self.tank_cap / 10
        total = self.tank_cap * i / 10
        return f"+{step:.1f} L  (∑ {total:.0f} L)"

    def prepare_lin_measure(self):
        """Kennlinie auf linear setzen, damit der Sensor die rohe Füllhöhe anzeigt."""
        if not self.bus:
            return
        ident = list(range(0, 101, 10))
        try:
            send_fast_packet(self.bus, PROP_PGN, self.sensor_addr, PC_SOURCE_ADDR,
                             build_lin_table_write(ident))
            self.log_line(">> Kennlinie auf linear gesetzt – Sensor zeigt jetzt rohe Füllhöhe.")
        except (can.CanError, ValueError) as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    def capture_level(self, idx):
        """Aktuellen Füllstand in die Zeile idx übernehmen."""
        lvl = getattr(self, "_last_level", None)
        if lvl is None:
            self.log_line("Noch kein Füllstand empfangen – erst verbinden und warten.")
            return
        e = self.calib_height_entries[idx]
        e.delete(0, "end")
        e.insert(0, f"{lvl:.1f}")

    def _resample_to_height_grid(self, heights):
        """Aus (gemessene Höhe_i, Volumen_i = i·10 %) das Volumen an den
        Höhen-Gitterpunkten 0,10,…,100 % interpolieren (= Firmware-Kennlinie)."""
        volumes = [i * 10 for i in range(11)]
        table = []
        for H in range(0, 101, 10):
            if H <= heights[0]:
                v = volumes[0]
            elif H >= heights[-1]:
                v = volumes[-1]
            else:
                v = volumes[-1]
                for j in range(10):
                    if heights[j] <= H <= heights[j + 1]:
                        span = heights[j + 1] - heights[j]
                        v = (volumes[j] if span <= 0
                             else volumes[j] + (volumes[j+1]-volumes[j]) * (H-heights[j]) / span)
                        break
            table.append(max(0, min(100, round(v))))
        return table

    def send_calib_table(self):
        if not self.tank_cap:
            messagebox.showwarning("Kapazität fehlt",
                                   "Erst die Tankkapazität setzen (Bereich »Sensor "
                                   "konfigurieren«), dann die Tankform einmessen.")
            return
        try:
            heights = [float(e.get().replace(",", ".")) for e in self.calib_height_entries]
        except ValueError:
            messagebox.showerror("Ungültige Eingabe",
                                 "Bitte in jede Zeile den abgelesenen Füllstand (%) eintragen.")
            return
        if any(heights[i+1] < heights[i] for i in range(10)):
            messagebox.showerror("Nicht plausibel",
                                 "Der abgelesene Füllstand muss von oben nach unten steigen.\n"
                                 f"Eingetragen: {heights}")
            return
        pts = self._resample_to_height_grid(heights)
        try:
            send_fast_packet(self.bus, PROP_PGN, self.sensor_addr, PC_SOURCE_ADDR,
                             build_lin_table_write(pts))
            # berechnete Kennlinie auch in den Hauptfenster-Editor übernehmen
            for e, v in zip(self.lin_entries, pts):
                e.delete(0, "end")
                e.insert(0, str(v))
            self.update_lin_liters()
            self.log_line(f">> Tankform eingemessen und gesendet: {pts}")
        except (can.CanError, ValueError) as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    def _send_prop(self, payload, msg):
        if not self.bus:
            return
        try:
            send_fast_packet(self.bus, PROP_PGN, self.sensor_addr, PC_SOURCE_ADDR, payload)
            self.log_line(f">> {msg} (an 0x{self.sensor_addr:02X})")
        except can.CanError as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    def read_lin_table(self):
        if not self.bus:
            return
        try:
            send_fast_packet(self.bus, PROP_PGN, self.sensor_addr, PC_SOURCE_ADDR,
                             build_lin_table_read())
            self.log_line(f">> Stützstellen-Tabelle von 0x{self.sensor_addr:02X} angefragt …")
        except can.CanError as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    def write_lin_table(self):
        if not self.bus:
            return
        try:
            points = [int(e.get()) for e in self.lin_entries]
            payload = build_lin_table_write(points)
        except ValueError as e:
            self.log_line(f"Ungültige Tabelle: {e}")
            return
        try:
            send_fast_packet(self.bus, PROP_PGN, self.sensor_addr, PC_SOURCE_ADDR, payload)
            self.log_line(f">> Stützstellen an 0x{self.sensor_addr:02X}: {points}"
                          f" – warte auf Bestätigung …")
        except can.CanError as e:
            self.log_line(f"Senden fehlgeschlagen: {e}")

    # ---------------- GUI-Updates ----------------

    def poll(self):
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "level":
                    self.show_level(payload)
                elif kind == "temp":
                    self.lbl_temp.config(text=f"Temperatur: {payload:.2f} °C")
                elif kind == "lin_table":
                    for e, v in zip(self.lin_entries, payload):
                        e.delete(0, "end")
                        e.insert(0, str(v))
                    self.update_lin_liters()
                    self.log_line(f"Stützstellen vom Sensor gelesen: {payload}")
                elif kind == "lin_ack":
                    self.log_line(payload)
                elif kind == "calib_ack":
                    self.log_line(payload)
                else:
                    self.log_line(payload)
        except queue.Empty:
            pass
        self.root.after(POLL_MS, self.poll)

    def update_lin_liters(self):
        """Zeigt unter jeder Stützstelle die Menge in Litern (aus PGN-127505-Kapazität)."""
        for e, lbl in zip(self.lin_entries, self.lin_liters):
            if self.tank_cap is None:
                lbl.config(text="–")
                continue
            try:
                v = int(e.get())
                lbl.config(text=f"{self.tank_cap * v / 100:.1f} L" if 0 <= v <= 100 else "?")
            except ValueError:
                lbl.config(text="?")

    def show_level(self, d):
        self.last_rx = time.time()
        self.sensor_addr = d["src"]     # Sensor kann nach Arbitrierung umziehen
        self._last_level = d["level"]   # für »Übernehmen« im Kalibrier-Fenster
        cap_changed = d["cap"] is not None and d["cap"] != self.tank_cap
        if cap_changed:
            self.tank_cap = d["cap"]
            self.update_lin_liters()
        # Live-Anzeige im Kalibrier-Fenster, falls geöffnet
        if getattr(self, "_calib_win", None) and self._calib_win.winfo_exists():
            if d["level"] is not None:
                self.calib_level_lbl.config(text=f"aktueller Füllstand: {d['level']:.1f} %")
            if cap_changed:
                self.calib_cap_lbl.config(text=f"Tankkapazität: {self.tank_cap:.0f} L")
                for i, fl in enumerate(self.calib_fill_labels):
                    fl.config(text=self._fill_step_text(i))
        lvl = d["level"]
        if lvl is not None:
            self.lbl_level.config(text=f"{lvl:.1f} %")
            self.bar["value"] = max(0.0, min(100.0, lvl))
            self.history.append((time.time(), lvl))
            cutoff = time.time() - HISTORY_SECONDS
            while self.history and self.history[0][0] < cutoff:
                self.history.popleft()
            self.draw_graph()
        else:
            self.lbl_level.config(text="n/a")
        cap = f"{d['cap']:.0f} L" if d["cap"] is not None else "–"
        self.lbl_detail.config(
            text=f"Fluid: {FLUID_TYPES.get(d['fluid'], d['fluid'])}   "
                 f"Kapazität: {cap}   Instanz: {d['instance']}   "
                 f"Quelle: 0x{d['src']:02X}")

    def draw_graph(self):
        c = self.canvas
        c.delete("all")
        w = max(c.winfo_width(), 50)
        h = max(c.winfo_height(), 50)
        pad = 24
        # Gitter + Beschriftung 0/50/100 %
        for pct in (0, 50, 100):
            y = h - pad - (h - 2 * pad) * pct / 100
            c.create_line(pad, y, w - 4, y, fill="#ddd")
            c.create_text(pad - 4, y, text=f"{pct}", anchor="e",
                          font=("Segoe UI", 7), fill="#666")
        if len(self.history) < 2:
            return
        now = time.time()
        pts = []
        for t, lvl in self.history:
            x = pad + (w - pad - 8) * (1 - (now - t) / HISTORY_SECONDS)
            y = h - pad - (h - 2 * pad) * max(0.0, min(100.0, lvl)) / 100
            pts += [x, y]
        c.create_line(*pts, fill="#0066cc", width=2)

    def watchdog(self):
        """Warnt, wenn 3 s keine Fluid-Level-Frames mehr kommen."""
        if self.bus is not None:
            if self.last_rx and time.time() - self.last_rx > 3:
                self.status.config(text="verbunden – keine Daten!", foreground="orange")
            elif self.last_rx:
                self.status.config(text=f"verbunden ({self.channel.get()})",
                                   foreground="green")
        self.root.after(1000, self.watchdog)

    def log_line(self, text):
        self.log.config(state="normal")
        self.log.insert("end", f"{time.str