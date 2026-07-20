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
