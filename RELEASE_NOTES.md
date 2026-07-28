## Firmware 1.2.9

### Sicherung der Konfiguration
- Neue BLE-Kommandos `CAL` (Kalibrierwert abfragen) und `CAL <n>`
  (Kalibrierwert direkt setzen). Damit kann die App die komplette
  Konfiguration eines Sensors sichern und auf einen anderen einspielen –
  nützlich beim Gerätetausch, nach einem Werksreset oder um einen zweiten
  identischen Tank gleich einzurichten. Der Kalibrierwert lässt sich so
  ohne vollen Tank wiederherstellen.
- Nicht Teil der Sicherung sind die geclaimte NMEA2000-Quelladresse (sie
  wird am Bus ohnehin neu ausgehandelt) und interne Marker.

### Variantenkennung
- Die `STAT`-Zeile meldet zusätzlich `HWV=<variante>` (1000 = Drucksensor
  V1, 1001 = Drucksensor V2, 1002 = Ultraschall). Erster Schritt zum
  Variantenmechanismus; die App zeigt die Variante ab 1.4.8 unter „Modul"
  an. Ältere Apps ignorieren das Feld.

### Behoben
- Ein fabrikneuer oder zurückgesetzter Sensor meldete fälschlich
  „kalibriert" (`CAL=1`), obwohl keine Kalibrierung vorlag. Der
  Werkszustand wird jetzt ausdrücklich als unkalibriert markiert.

### Intern
- Die OTA-CRC32 läuft tabellenbasiert und damit rund achtmal schneller;
  der Wert ist bit-identisch zur bisherigen Berechnung und stimmt weiter
  mit dem Bootloader überein.
- Neu: `TESTPLAN.md` – Abhak-Checkliste für die Hardware-Tests vor einem
  Release.
- `ARCHITECTURE.md` beschreibt das Mono-Repo-Zielbild und den geplanten
  Variantenmechanismus.

## Firmware 1.2.8

### Namens-Synchronisation
- Wird der Sensorname per App (BLE) geändert, verschickt der Sensor sofort
  ein aktualisiertes PGN 126998 – PC-Tool und Plotter zeigen den neuen
  Namen ohne Neustart.
- Nach jedem Boot (insbesondere nach einem Werksreset) meldet der Sensor
  seinen Namensstand einmalig unaufgefordert auf dem NMEA-Bus.
- PC-Tool: fragt bei jedem Address Claim den Namen des Sensors aktiv neu
  an – die Anzeige bleibt auch bei Sensoren mit älterer Firmware aktuell.

### Struktur (kein Funktionsunterschied)
- Die große main.c wurde in Module aufgeteilt: app_config (Config/EEPROM),
  sensor (Messung/DAC), nmea_app (NMEA2000-Handler), ble_app
  (BLE-Kommandos). Verhalten und Protokolle sind unverändert; jede Stufe
  wurde einzeln auf der Hardware getestet.

### Build
- Neuer Post-Build-Schritt: tools/make_app_meta_hex.py erzeugt nach jedem
  Build automatisch eine kombinierte HEX (App + passende DFU-Metadaten) und
  eine aktuelle meta.hex – das manuelle Erzeugen der Meta entfällt
  (siehe tools/README_build.md).
- Compiler-Warnungen beseitigt; der Build ist warnungsfrei.
