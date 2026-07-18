#!/usr/bin/env python3
"""
NMEA2000-Reader für den CAN-Füllstandsensor (STM32G0B1) über PEAK PCAN-USB.

Liest und dekodiert:
  - PGN 127505 Fluid Level (Füllstand, Fluidtyp, Kapazität)
  - PGN 60928  ISO Address Claim
  - PGN 126996 Product Information (Fast Packet, auf Anfrage)

Voraussetzungen:
  - PEAK-Treiber / PCAN-Basic installiert
  - pip install python-can

Aufruf:
  python nmea2000_reader.py                  # nur mithören
  python nmea2000_reader.py --request        # zusätzlich Product-Info anfragen
  python nmea2000_reader.py --channel PCAN_USBBUS2
"""

import argparse
import struct
import sys
import time

import can

BITRATE = 250000          # NMEA2000-Standard
PC_SOURCE_ADDR = 0x25     # Quelladresse dieses PC-Tools am Bus
SENSOR_ADDR = 0x21        # srcAdr aus der Firmware (main.c)

FLUID_TYPES = {
    0: "Fuel", 1: "Water", 2: "Gray Water", 3: "Live Well",
    4: "Oil", 5: "Black Water", 6: "Gasoline",
}


# ---------------------------------------------------------------- CAN-ID <-> PGN

def decode_can_id(can_id: int):
    """29-bit-ID -> (priority, pgn, destination, source)."""
    prio = (can_id >> 26) & 0x7
    dp = (can_id >> 24) & 0x3       # EDP+DP (Data-Page-Bits gehören zur PGN!)
    pf = (can_id >> 16) & 0xFF
    ps = (can_id >> 8) & 0xFF
    src = can_id & 0xFF
    if pf < 240:                    # PDU1: PS = Zieladresse
        return prio, (dp << 16) | (pf << 8), ps, src
    return prio, (dp << 16) | (pf << 8) | ps, 0xFF, src   # PDU2: Broadcast


def encode_can_id(prio: int, pgn: int, dest: int, src: int) -> int:
    pf = (pgn >> 8) & 0xFF
    if pf < 240:
        return (prio & 7) << 26 | (pgn & 0x1FF00) << 8 | (dest & 0xFF) << 8 | (src & 0xFF)
    return (prio & 7) << 26 | (pgn & 0x1FFFF) << 8 | (src & 0xFF)


# ---------------------------------------------------------------- Decoder

def decode_fluid_level(data: bytes, src: int):
    if len(data) < 7:
        return None
    instance = data[0] & 0x0F
    fluid = (data[0] >> 4) & 0x0F
    raw_level = struct.unpack_from("<h", data, 1)[0]
    raw_cap = struct.unpack_from("<I", data, 3)[0]
    level = None if raw_level == 0x7FFE else raw_level / 250.0      # % (Auflösung 0,004 %)
    cap = None if raw_cap == 0xFFFFFFFE else raw_cap * 0.1          # Liter
    lvl_s = f"{level:6.2f} %" if level is not None else "  n/a  "
    cap_s = f"{cap:8.1f} L" if cap is not None else "   n/a   "
    return (f"[0x{src:02X}] Fluid Level  inst={instance}  "
            f"typ={FLUID_TYPES.get(fluid, fluid)}  Füllstand={lvl_s}  Kapazität={cap_s}")


def decode_address_claim(data: bytes, src: int):
    if len(data) < 8:
        return None
    name = struct.unpack("<Q", data)[0]
    unique = name & 0x1FFFFF
    mfr = (name >> 21) & 0x7FF
    func = (name >> 40) & 0xFF
    dev_class = (name >> 49) & 0x7F
    return (f"[0x{src:02X}] Address Claim  unique={unique}  mfr={mfr}  "
            f"function={func}  class={dev_class}")


def parse_address_claim(data: bytes):
    """Zerlegt den 64-bit-NAME eines Address Claims in seine Felder."""
    if len(data) < 8:
        return None
    name = struct.unpack("<Q", data)[0]
    return {
        "unique": name & 0x1FFFFF,
        "mfr": (name >> 21) & 0x7FF,
        "dev_instance": (name >> 32) & 0xFF,
        "function": (name >> 40) & 0xFF,
        "dev_class": (name >> 49) & 0x7F,
    }


def decode_heartbeat(data: bytes, src: int):
    """PGN 126993 Heartbeat: Intervall (uint16 LE, ms) + Sequenzzaehler."""
    if len(data) < 3:
        return None
    return {"src": src,
            "interval_ms": struct.unpack_from("<H", data, 0)[0],
            "seq": data[2]}


def decode_iso_request(data: bytes, dest: int, src: int):
    """PGN 59904 ISO Request: 3 Bytes = angefragte PGN (LE)."""
    if len(data) < 3:
        return None
    pgn = data[0] | (data[1] << 8) | (data[2] << 16)
    dest_s = "Broadcast" if dest == 0xFF else f"0x{dest:02X}"
    return f"[0x{src:02X}] ISO Request PGN {pgn} an {dest_s}"


TEMP_SOURCES = {
    0: "Sea", 1: "Outside", 2: "Inside", 3: "Engine Room", 4: "Main Cabin",
    5: "Live Well", 6: "Bait Well", 7: "Refrigeration", 8: "Heating",
}


def decode_temperature(data: bytes, src: int):
    """PGN 130312: Actual Temperature als uint16 LE in 0,01 K (Bytes 3-4)."""
    if len(data) < 5:
        return None
    raw = struct.unpack_from("<H", data, 3)[0]
    if raw in (0xFFFF, 0xFFFE):
        return f"[0x{src:02X}] Temperatur  n/a"
    t = raw / 100.0 - 273.15
    return (f"[0x{src:02X}] Temperatur  inst={data[1]}  "
            f"quelle={TEMP_SOURCES.get(data[2], data[2])}  {t:6.2f} °C")


def decode_product_info(data: bytes, src: int):
    def s(off):
        return bytes(data[off:off + 32]).split(b"\x00")[0].split(b"\xff")[0].decode("ascii", "replace")
    if len(data) < 132:
        return f"[0x{src:02X}] Product Info (unvollständig, {len(data)} B)"
    version = struct.unpack_from("<H", data, 0)[0]
    code = struct.unpack_from("<H", data, 2)[0]
    return (f"[0x{src:02X}] Product Info  N2K-Version={version / 1000:.3f}  ProductCode={code}\n"
            f"          ModelID='{s(4)}'  SW='{s(36)}'  Version='{s(68)}'  Serial='{s(100)}'")


def decode_config_info(data: bytes, src: int):
    """PGN 126998 Configuration Information: bis zu drei variable Strings
    (je [Laenge n+2][0x01 = ASCII][n Zeichen]): Installation Description 1,
    Installation Description 2, Manufacturer Information. Liefert immer eine
    Liste mit 3 Strings; faellt beim Altformat (roher String, FW <= 1.2.4)
    auf diesen als erstes Feld zurueck."""
    fields = []
    pos = 0
    while pos + 2 <= len(data) and len(fields) < 3:
        ln = data[pos]
        if ln < 2 or pos + ln > len(data):
            fields = []                       # unplausibel -> Altformat
            break
        raw = bytes(data[pos + 2:pos + ln])
        fields.append(raw.decode("ascii", "replace").strip("\x00").strip())
        pos += ln
    if not fields:
        txt = data.split(b"\xff")[0].decode("ascii", "replace").strip()
        return [txt, "", ""]
    while len(fields) < 3:
        fields.append("")
    return fields


# ---------------------------------------------------------------- Fast Packet

class FastPacketAssembler:
    """Reassembliert NMEA2000 Fast Packets, gruppiert nach (PGN, Quelle)."""

    def __init__(self):
        self.buf = {}   # (pgn, src) -> dict(len, data, next)

    def feed(self, pgn: int, src: int, frame: bytes):
        key = (pgn, src)
        counter = frame[0] & 0x1F
        if counter == 0:                                  # erster Frame: Byte1 = Gesamtlänge
            self.buf[key] = {"len": frame[1], "data": bytearray(frame[2:8]), "next": 1}
            return self._check(key)
        st = self.buf.get(key)
        if st is None or counter != st["next"]:
            self.buf.pop(key, None)                       # Sequenzfehler -> verwerfen
            return None
        st["data"] += frame[1:8]
        st["next"] += 1
        return self._check(key)

    def _check(self, key):
        st = self.buf[key]
        if len(st["data"]) >= st["len"]:
            data = bytes(st["data"][: st["len"]])
            del self.buf[key]
            return data
        return None


# ---------------------------------------------------------------- Group Function (PGN 126208)

GF_CODES = {0: "Request", 1: "Command", 2: "Acknowledge"}
GF_PARAM_ERR = {0: "OK", 1: "ungültiges Feld", 2: "vorübergehend nicht möglich",
                3: "außerhalb Bereich", 4: "nicht unterstützt"}


def send_fast_packet(bus: can.Bus, pgn: int, dest: int, src: int, payload: bytes,
                     prio: int = 6, seq: int = 0):
    """Sendet einen NMEA2000 Fast Packet: Frame 0 = [Seq, Länge, 6 B], danach [n, 7 B]."""
    can_id = encode_can_id(prio, pgn, dest, src)
    frames = [bytes([(seq << 5) | 0, len(payload)]) + payload[:6].ljust(6, b"\xff")]
    pos, counter = 6, 1
    while pos < len(payload):
        frames.append(bytes([(seq << 5) | counter]) + payload[pos:pos + 7].ljust(7, b"\xff"))
        pos += 7
        counter += 1
    for f in frames:
        bus.send(can.Message(arbitration_id=can_id, data=f, is_extended_id=True))


def build_gf_command_127505(instance=None, fluid_type=None, capacity_liters=None) -> bytes:
    """Command Group Function für PGN 127505 (byte-aligned Feldwerte, wie die Firmware)."""
    pairs = b""
    n = 0
    if instance is not None:
        pairs += bytes([1, instance & 0x0F]); n += 1
    if fluid_type is not None:
        pairs += bytes([2, fluid_type & 0x0F]); n += 1
    if capacity_liters is not None:
        pairs += bytes([4]) + struct.pack("<I", int(capacity_liters * 10)); n += 1
    return bytes([1, 127505 & 0xFF, (127505 >> 8) & 0xFF, (127505 >> 16) & 0xFF,
                  0xFF, n]) + pairs


def build_gf_command_name(name: str) -> bytes:
    """Command Group Function (PGN 126208) auf PGN 126998, Feld 1 =
    Installation Description 1 (Sensorname) - derselbe Weg, ueber den auch
    ein Plotter den Namen setzt. Stringformat: [Laenge n+2][0x01=ASCII][Zeichen].
    Leerer String loescht den Namen (der BLE-Name faellt beim naechsten Boot
    auf LevelSense-<UID> zurueck); die Firmware kuerzt auf 24 Zeichen."""
    raw = name.encode("ascii", "replace")[:24]
    return (bytes([1, 126998 & 0xFF, (126998 >> 8) & 0xFF, (126998 >> 16) & 0xFF,
                   0xFF, 1, 1, len(raw) + 2, 0x01]) + raw)


def decode_gf(data: bytes, src: int):
    """Dekodiert eine reassemblierte Group Function (v.a. Acknowledge)."""
    if len(data) < 6:
        return None
    fn = data[0]
    target = data[1] | (data[2] << 8) | (data[3] << 16)
    if fn == 2:
        pgn_err = data[4] & 0x0F
        n = data[5]
        errs = []
        for i in range(min(n, (len(data) - 6) * 2)):
            b = data[6 + i // 2]
            code = (b >> 4) if (i & 1) else (b & 0x0F)
            errs.append(GF_PARAM_ERR.get(code, str(code)))
        all_ok = (pgn_err == 0) and all(e == "OK" for e in errs)
        status = "OK" if all_ok else f"PGN-Fehler={pgn_err}"
        return (f"[0x{src:02X}] GF-Acknowledge für PGN {target}: {status}"
                + (f"  Parameter: {', '.join(errs)}" if errs else ""))
    return f"[0x{src:02X}] Group Function {GF_CODES.get(fn, fn)} für PGN {target}"


# ---------------------------------------------------------------- Proprietär (PGN 126720): Tank-Linearisierung

PROP_PGN = 126720
PROP_HEADER = b"\xfe\x9f"      # MFR 2046, Industry Group 4
PROP_CMD_SET_LIN = 0x01
PROP_CMD_GET_LIN = 0x02
PROP_CMD_CALIB = 0x03         # aktuellen Druck als 100 % kalibrieren
PROP_CMD_RESET = 0x04         # Kalibrierung zurücksetzen
PROP_CMD_FRESET = 0x05        # Werksreset: kompletten Config löschen + Neustart


def build_lin_table_write(points) -> bytes:
    """11 Stützstellen (Volumen-% bei 0,10,..,100 % Füllhöhe) -> Schreibkommando."""
    if len(points) != 11 or any(not 0 <= p <= 100 for p in points):
        raise ValueError("11 Werte im Bereich 0..100 erforderlich")
    if any(points[i+1] < points[i] for i in range(10)):
        raise ValueError("Werte müssen monoton steigend sein")
    return PROP_HEADER + bytes([PROP_CMD_SET_LIN]) + bytes(points)


def build_lin_table_read() -> bytes:
    return PROP_HEADER + bytes([PROP_CMD_GET_LIN])


def build_calib_max() -> bytes:
    """Aktuellen Füllstand am Sensor als 100 % (Maximalwert) kalibrieren."""
    return PROP_HEADER + bytes([PROP_CMD_CALIB])


def build_factory_reset() -> bytes:
    """Werksreset: löscht Kalibrierung, Tankform, Instanz, Name und die
    gespeicherte Adresse; der Sensor startet neu (Adresse 0x21)."""
    return PROP_HEADER + bytes([PROP_CMD_FRESET])


def build_commanded_address(name8: bytes, new_addr: int) -> bytes:
    """PGN 65240 Commanded Address (ISO 11783-5): 8 Byte NAME des Zielgeräts
    (aus dessen Address Claim) + 1 Byte neue Quelladresse (0..251)."""
    if len(name8) != 8:
        raise ValueError("NAME muss 8 Byte lang sein")
    if not 0 <= new_addr <= 251:
        raise ValueError("Adresse muss 0..251 sein")
    return bytes(name8) + bytes([new_addr])


def build_calib_reset() -> bytes:
    """Maximalwert-Kalibrierung auf Werkswert zurücksetzen."""
    return PROP_HEADER + bytes([PROP_CMD_RESET])


def decode_prop(data: bytes, src: int):
    """Dekodiert proprietäre Antworten des Sensors (0x81 = Schreibstatus, 0x82 = Tabelle)."""
    if len(data) < 3 or data[:2] != PROP_HEADER:
        return None
    if data[2] == 0x81 and len(data) >= 4:
        ok = data[3] == 0
        return ("lin_ack", f"[0x{src:02X}] Stützstellen-Tabelle: "
                           + ("gespeichert" if ok else f"abgelehnt (Code {data[3]})"))
    if data[2] == 0x82 and len(data) >= 14:
        pts = list(data[3:14])
        return ("lin_table", pts)
    if data[2] == 0x83 and len(data) >= 4:
        ok = data[3] == 0
        return ("calib_ack", f"[0x{src:02X}] 100%-Kalibrierung: "
                             + ("übernommen" if ok else "abgelehnt (kein gültiger Druck)"))
    if data[2] == 0x84 and len(data) >= 4:
        return ("calib_ack", f"[0x{src:02X}] Kalibrierung auf Werkswert zurückgesetzt")
    if data[2] == 0x85 and len(data) >= 4:
        return ("calib_ack", f"[0x{src:02X}] Werksreset bestätigt – Sensor startet neu "
                             f"(neue Adresse 0x21)")
    return None


# ---------------------------------------------------------------- Requests

def send_iso_request(bus: can.Bus, pgn: int, dest: int):
    """PGN 59904 ISO Request: 3 Datenbytes = angefragte PGN (little endian)."""
    can_id = encode_can_id(6, 59904, dest, PC_SOURCE_ADDR)
    data = bytes([pgn & 0xFF, (pgn >> 8) & 0xFF, (pgn >> 16) & 0xFF])
    bus.send(can.Message(arbitration_id=can_id, data=data, is_extended_id=True))
    print(f">> ISO Request PGN {pgn} an 0x{dest:02X}")


# ---------------------------------------------------------------- Main

def main():
    ap = argparse.ArgumentParser(description="NMEA2000-Reader für PCAN-USB")
    ap.add_argument("--channel", default="PCAN_USBBUS1", help="PCAN-Kanal (Default: PCAN_USBBUS1)")
    ap.add_argument("--request", action="store_true",
                    help="beim Start Product-Info und Address-Claim anfragen")
    ap.add_argument("--raw", action="store_true", help="alle Frames zusätzlich roh ausgeben")
    args = ap.parse_args()

    try:
        bus = can.Bus(interface="pcan", channel=args.channel, bitrate=BITRATE)
    except Exception as e:
        sys.exit(f"PCAN konnte nicht geöffnet werden ({e}).\n"
                 f"Treiber installiert? Adapter angeschlossen? Kanal korrekt (--channel)?")

    print(f"Verbunden: {args.channel} @ {BITRATE // 1000} kbit/s – Strg+C beendet.")
    fp = FastPacketAssembler()

    if args.request:
        send_iso_request(bus, 60928, 0xFF)            # Address Claim an Broadcast
        time.sleep(0.2)
        send_iso_request(bus, 126996, SENSOR_ADDR)    # Product Info direkt an den Sensor

    try:
        while True:
            msg = bus.recv(timeout=1.0)
            if msg is None or not msg.is_extended_id:
                continue
            prio, pgn, dest, src = decode_can_id(msg.arbitration_id)
            data = bytes(msg.data)

            if args.raw:
                print(f"   raw id=0x{msg.arbitration_id:08X} pgn={pgn} src=0x{src:02X} "
                      f"data={data.hex(' ')}")

            out = None
            if pgn == 127505:
                out = decode_fluid_level(data, src)
            elif pgn == 130312:
                out = decode_temperature(data, src)
            elif pgn == 60928:
                out = decode_address_claim(data, src)
            elif pgn in (126996, 126998, 126208, PROP_PGN):
                full = fp.feed(pgn, src, data)
                if full is not None:
                    if pgn == 126996:
                        out = decode_product_info(full, src)
                    elif pgn == 126208:
                        out = decode_gf(full, src)
                    elif pgn == PROP_PGN:
                        r = decode_prop(full, src)
                        if r:
                            out = (r[1] if r[0] == "lin_ack"
                                   else f"[0x{src:02X}] Stützstellen: {r[1]}")
                    else:
                        txt = full.split(b"\xff")[0].decode("ascii", "replace")
                        out = f"[0x{src:02X}] Device Info: '{txt}'"
            if out:
                print(f"{time.strftime('%H:%M:%S')}  {out}")
    except KeyboardInterrupt:
        print("\nBeendet.")
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
