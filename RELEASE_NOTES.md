## Firmware 1.2.4 (2026-07-08)

### Zuverlässigkeit
- **Watchdog (IWDG, ~8 s):** hängende Zustände führen jetzt zu einem automatischen Neustart statt zum dauerhaften Ausfall.
- **Bus-Off-Recovery:** nach Kurzschluss/Störung auf dem CAN-Bus nimmt das Gerät automatisch wieder am Busverkehr teil (vorher: dauerhaft stumm). Fehleranzeige über die LED.
- **CAN-Sendewiederholung aktiviert** (AutoRetransmission): keine verlorenen Frames mehr bei Arbitrierungsverlust oder Störung.
- **CAN-Bit-Timing toleranter** (SJW 4, Samplepoint 75 %): robuster gegenüber Takt-Drift, weiterhin 250 kbit/s.
- Boot bleibt nicht mehr hängen bei unbekanntem DAC-Kalibrierwert; transiente CAN-RX-Fehler blockieren das Gerät nicht mehr.
- Division durch Null bei der 100-%-Kalibrierung abgesichert (Mindestdruck erforderlich).

### NMEA2000
- **Neu: Heartbeat (PGN 126993)** alle 60 s und **PGN-Liste (PGN 126464)** auf Anfrage.
- **Sendezyklen normgerecht:** Fluid Level (127505) alle 2,5 s, Temperatur (130312) alle 2 s – vorher 200 ms (~12-fache Buslast). Längere Intervalle am Plotter sind beabsichtigt.
- ISO Requests: Broadcast-Anfragen (Product Info, Device Info, Address Claim) werden jetzt beantwortet; Anfrage auf 127505 liefert sofort einen Messwert.
- Product Info korrigiert: Load Equivalency jetzt 2 (= 100 mA) statt fälschlich 48 (= 2,4 A).

### Intern
- Ungenutztes EEPROM-Emulationsmodul und toter Debug-Code entfernt (−650 Zeilen).
- Sensor- und PGN-Berechnung auf Integer-Arithmetik umgestellt (keine Soft-Float-Bibliothek mehr nötig).
- `nmea200.*` in `nmea2000.*` umbenannt; Release-Notes kommen ab jetzt aus `RELEASE_NOTES.md`.
- Bedienungsanleitung aktualisiert.

### Update
OTA über die Handy-App (DFU-Kommando) oder per SWD. `Fuellstandsensor_v1.2.4.bin` ist OTA-tauglich.
