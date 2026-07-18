## Firmware 1.2.6

### Bluetooth-Name
- Der BLE-Modulname folgt jetzt automatisch dem gespeicherten Sensornamen:
  Beim Boot wird der Modulname ausgelesen und nur bei Abweichung neu
  geschrieben (schont das Modul-Flash, kein unnötiger Modul-Neustart).
- Fabrikneue oder zurückgesetzte Sensoren heißen beim BLE-Scan jetzt
  `LevelSense-<UID>` – mehrere Sensoren sind damit unterscheidbar.
- Umbenennen vom Plotter (Group Function) zieht den Bluetooth-Namen sofort
  mit; ein NAME-Kommando mit unverändertem Namen löst keinen Modul-Neustart
  mehr aus (Verbindung bleibt bestehen).

### Werksreset & Adressverwaltung
- Neu: Werksreset per BLE-Kommando `FACTORYRESET` (App ab 1.4.5) oder per
  proprietärem PGN 126720 (PC-Tool). Löscht Kalibrierung, Tankform,
  Konfiguration, Name und gespeicherte Adresse; der Sensor startet neu.
- Neu: Commanded Address (PGN 65240) – Plotter oder PC-Tool können dem
  Sensor eine feste Quelladresse zuweisen.

### PC-Tool
- Mehrsensor-Betrieb: Auswahlliste mit Wiedererkennung nach Adresswechsel
  (Unique Number), Address-Claim-, Heartbeat- und Mess-Takt-Anzeige,
  NMEA-Logfenster mit Filter.
- Sensorname direkt aus dem Tool setzbar (wie vom Plotter, per Group
  Function); unbenannte Sensoren zeigen ihre UID im Dropdown.
- Werksreset und Adress-Zuweisung per Knopf.

### Intern
- Host-Testsuite auf 33 Tests erweitert (u. a. Werksreset).

### Update
OTA über die App (DFU) oder SWD. `Fuellstandsensor_v1.2.6.bin` ist OTA-tauglich.

## Firmware 1.2.5

### Sensorname
- Der Sensor kann jetzt einen frei vergebenen Namen speichern (bis 24 Zeichen).
  Er wird als Installation Description 1 in PGN 126998 (Configuration
  Information) gemeldet und erscheint in der Geräteliste vieler Plotter.
- Setzbar über die App (Kommando NAME, ab App 1.4.4) oder direkt vom Plotter
  per Group Function (126208 → 126998, Feld 1). NAME ohne Argument liefert
  den gespeicherten Namen zurück.

### Konfigurationsspeicher
- Neues Speicherformat (v2, 64 Byte) mit automatischer Migration: Beim ersten
  Start nach dem Update werden Kalibrierung, Tankform, Adresse und alle
  Einstellungen unverändert übernommen – nichts muss neu eingestellt werden.
- Achtung: Ein späteres Downgrade auf 1.2.4 oder älter kann das neue Format
  nicht lesen und fällt auf Werkseinstellungen zurück (Kalibrierung dann neu).
- Migration durch eine Host-Testsuite abgesichert (tests/, 27 Tests).

### Update
OTA über die App (DFU) oder SWD. `Fuellstandsensor_v1.2.5.bin` ist OTA-tauglich.

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
