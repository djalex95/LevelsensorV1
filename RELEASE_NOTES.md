## Firmware 1.2.10

Erste Firmware fuer die Platine V2 (STM32G0B1KBU6 ohne N-Suffix) mit dem
Wuerth-Drucksensor WSEN-PDMS. Die V1-Linie ist mit 1.2.9 eingefroren
(Branch `v1-legacy`); dieses Release enthaelt daher keine Binaries fuer
die Variante 1000 mehr.

### Hardware-Varianten
- Neuer Treiber fuer den WSEN-PDMS: Variante 1001 (+/-10 kPa) und 1003
  (+/-1 kPa, flache Tanks). Beide nutzen dieselbe Platine V2 und
  unterscheiden sich nur im bestueckten Sensor.
- Der Build verlangt jetzt eine ausdrueckliche Variante
  (`make HW_VARIANT=1003`); ein Build ohne Angabe bricht ab. Die CI
  weist das nach.
- Jede Variante prueft beim Kompilieren, dass Include-Pfad und
  HW_VARIANT-Symbol zusammenpassen - eine vertauschte Firmware faellt
  sonst nicht als Fehler auf, sondern nur als zehnfach falscher
  Fuellstand.

### Variantenkennung mit Platinen-Stand
- Variante und Platinen-Revision sind nach aussen EINE Kennung, z. B.
  `1003A`: die Zahl entscheidet ueber die passende Firmware, der
  Buchstabe ist der Hardware-Stand der Platine (rein informativ).
- Die `STAT`-Zeile meldet `HWV=1003A`; das fruehere Feld `HW=` entfaellt.
  Die NMEA2000-Produktinfo (PGN 126996) meldet die Kennung im
  ModelVersion-Feld statt des bisherigen "Rev 1000".
- Die App filtert das Update-Angebot ab Version 1.4.9 nach dieser
  Kennung; aeltere Apps zeigen bei der Variante einen Strich.

### Behoben
- Kompletter Ausfall des NMEA2000-Empfangs nach einer
  CubeMX-Neugenerierung: `ExtFiltersNbr` stand auf 0, damit wurden alle
  29-Bit-Frames verworfen (Fuellstand wurde weiter gesendet, aber
  Address Claim und Produktabfragen blieben unbeantwortet). Details in
  Issue #1.

### Intern
- Projekt auf STM32G0B1KBUx umbenannt (Linkerscripte, Startup, .cproject);
  Flusskontrolle der BLE-UART bewusst deaktiviert (Modul nutzt bei
  115200 Baud kein RTS/CTS, Pins sind auf der Platine vorhanden).
- Host-Tests fuer die Umrechnung aller Varianten (`tests/run_tests.sh`),
  laufen in der CI bei jedem Push.

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
