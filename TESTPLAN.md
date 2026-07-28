# Testplan – Füllstandsensor

Abhak-Liste für die Hardware-Tests vor einem Release. Der Plan ist bewusst
so geschnitten, dass jeder Block **einzeln** durchlaufen werden kann: Wer nur
die Firmware angefasst hat, testet Block A und die Regressionsliste.

Vorgehen: Testlauf-Kopf ausfüllen, Kästchen abhaken, bei Abweichungen die
Zeile *nicht* abhaken und unter „Befunde" notieren. Nach dem Release den
ausgefüllten Stand als Commit ablegen (oder in `docs/Testlogs/` archivieren),
damit nachvollziehbar bleibt, was womit getestet wurde.

---

## Testlauf

| Feld | Wert |
|---|---|
| Datum | |
| Firmware-Version | 1.2.9 |
| App-Version | 1.4.8 (+20) |
| Bootloader-Version | 1.1.1 |
| Testsensor (UID) | |
| Handy / Android-Version | |
| Tester | Alex |

---

## A – Firmware 1.2.9

### A.1 Grundfunktion (Regression nach dem main.c-Split)

- [ ] Sensor startet, Status-LED verhält sich wie gewohnt
- [ ] Füllstand `L` plausibel und stabil (kein Sprung/Rauschen gegenüber 1.2.8)
- [ ] Temperatur `T` plausibel, Vorzeichen korrekt
- [ ] NMEA2000: Füllstand (127505) und Temperatur (130312) kommen am PC-Tool an
- [ ] Adressvergabe/Claim läuft durch, Gerät erscheint in der Geräteliste
- [ ] Kennlinie: `LIN` lesen → Werte wie gespeichert; `LIN …` schreiben → `OK LIN`
- [ ] `FLUID` / `CAP` / `INST` setzen, Sensor neu starten → Werte sind erhalten

### A.2 Neu: Variantenkennung HWV

> Die `STAT`-Zeile läuft nur über BLE (nicht über CAN), das PC-Tool zeigt sie
> nicht. Sichtbar wird sie in der App unter **„Modul"** – dort stehen
> Firmware-Version, HW-Revision und ab 1.4.8 auch die HW-Variante. Wer die
> Rohzeile sehen will, nimmt ein BLE-Terminal (nRF Connect / Proteus Connect).

- [ ] App zeigt unter „Modul" die Zeile **„HW-Variante: Drucksensor V1 (1000)"**
- [ ] Firmware-Version und HW-Revision daneben weiterhin korrekt
      (Zeile wird nicht abgeschnitten, Felder nicht verschoben)
- [ ] **Rückwärtskompatibilität:** alte App 1.4.7 gegen neue Firmware –
      Anzeige vollständig, kein Parse-Fehler durch das Zusatzfeld
- [ ] **Vorwärtskompatibilität:** neue App 1.4.8 gegen alte Firmware 1.2.8 –
      Zeile „HW-Variante" fehlt einfach, alles andere unverändert

### A.3 Neu: Kalibrierstatus im Werkszustand (Bugfix)

- [ ] `FACTORYRESET` senden → Sensor startet neu
- [ ] App zeigt danach **„nicht kalibriert"** (CAL=0) – das war der Bug
- [ ] PC-Tool zeigt denselben Zustand
- [ ] Druck anlegen, `CAL100` → `OK CAL100`, App zeigt **„kalibriert"** (CAL=1)
- [ ] Sensor neu starten → bleibt „kalibriert", Füllstand rechnet korrekt
- [ ] `CALRESET` → wieder „nicht kalibriert", überlebt Neustart

### A.4 Namensabgleich (Regression der letzten Fixes)

- [ ] Name in der App ändern → PC-Tool zeigt den neuen Namen **ohne Neustart**
- [ ] Name im PC-Tool ändern → BLE-Name zieht nach
- [ ] Kein falscher Hinweis „Bluetooth-Name weicht ab" mehr in der App
- [ ] Nach `FACTORYRESET`: Name `LevelSense-<UID>` erscheint in App **und**
      PC-Tool, **ohne** Sensor entfernen/neu hinzufügen und ohne Tool-Neustart

---

## B – App 1.4.8

### B.1 Neu: Verbindung im Hintergrund freigeben (15 s)

- [ ] App verbunden, dann Handy sperren bzw. App in den Hintergrund →
      nach ca. 15 s trennt die Verbindung
- [ ] Zweites Handy kann sich danach verbinden (Sensor ist nicht mehr blockiert)
- [ ] **Kurz weg, schnell zurück:** App < 15 s in den Hintergrund und wieder
      nach vorn → Verbindung bleibt bestehen, keine Trennung
- [ ] App wieder in den Vordergrund (nach Trennung) → verbindet automatisch
      neu, Werte laufen zügig wieder ein
- [ ] App komplett schließen → Verbindung sauber getrennt, kein Reconnect-Timer
      im Hintergrund
- [ ] **Wichtig:** OTA-Update läuft, App kurz in den Hintergrund →
      Update wird **nicht** abgebrochen

### B.2 Neu: Sicherung der Konfiguration

> Für den vollständigen Test werden **zwei** Sensoren gebraucht (oder ein
> Sensor, der zwischendurch per `FACTORYRESET` geleert wird). Die Sicherung
> liegt als lesbare JSON-Datei vor – sie darf ruhig einmal im Texteditor
> geöffnet und gegengelesen werden.

- [ ] „Sichern" schreibt eine Datei `backup_<Name>_<Zeitstempel>.json`
- [ ] Datei im Editor öffnen: Kalibrierwert, Kennlinie, Fluidtyp, Kapazität,
      Instanz und Name stehen drin und passen zum Sensor
- [ ] Quellblock enthält Firmware-Version, HW-Revision und HW-Variante
- [ ] Sensor per `FACTORYRESET` leeren → App zeigt Werkszustand
- [ ] „Einspielen" mit dieser Datei: Dialog listet alle Werte auf,
      „Abbrechen" schreibt nichts
- [ ] „Einspielen" bestätigen → alle Werte stehen wieder, Füllstand rechnet
      wie vorher (Kalibrierung ist zurück, **ohne** vollen Tank)
- [ ] Der Name kommt zuletzt: Modul startet danach neu, Verbindung trennt sich
      – das ist erwartet, danach normal neu verbinden
- [ ] Auf einen **zweiten** Sensor einspielen → verhält sich wie der erste
- [ ] Sicherung eines **unkalibrierten** Sensors einspielen → Zielsensor ist
      danach unkalibriert (`CALRESET`), kein Fehler
- [ ] **Fremde Datei** wählen (z. B. irgendein anderes JSON) → verständliche
      Meldung, es wird nichts geschrieben
- [ ] **Variantenwarnung:** in einer Sicherung `hw_variante` von Hand auf
      `1002` ändern → Dialog zeigt die rote Warnung, Einspielen bleibt möglich
- [ ] **Alte Firmware 1.2.8:** „Sichern" meldet sauber, dass Firmware 1.2.9
      nötig ist (der Sensor kennt `CAL` noch nicht) – kein Absturz, keine
      halbe Datei
- [ ] Getrennter Sensor: beide Schaltflächen sind ausgegraut, Hinweistext
      erscheint

### B.3 Regression

- [ ] Scan findet den Sensor, Hinzufügen funktioniert
- [ ] Tankform-Assistent schreibt und liest die Kennlinie korrekt
- [ ] Mehrere Sensoren gleichzeitig in der Liste: Namen/Werte nicht vertauscht
- [ ] Anzeige unverändert korrekt, wenn ein Sensor `HWV` **nicht** sendet
      (alte Firmware 1.2.8) – Feld ist optional

---

## C – Bootloader 1.1.1 (Flashen per ST-Link)

> Bootloader nur per ST-Link aufspielen. Vor dem Test die aktuelle App-Firmware
> als Backup bereithalten, falls ein Rückfall nötig wird.

- [ ] Bootloader per ST-Link geflasht, Sensor startet danach normal in die App
- [ ] Boot spürbar schneller / kein unnötiges Warten mehr beim Start
- [ ] App zeigt im Update-Dialog die Bootloader-Version **1.1.1**
- [ ] **Kompletter OTA-Update-Durchlauf** über die App: Übertragung,
      CRC-Prüfung, Neustart in die neue Firmware
- [ ] Nach dem Update meldet der Sensor die erwartete Version im STAT
- [ ] **DFU-Idle-Timeout:** in DFU-Modus wechseln (`DFU`), dann nichts tun →
      nach ca. 2 Minuten startet der Sensor von selbst wieder in die App
- [ ] **Abbruch mitten im Update:** App während der Übertragung schließen →
      Sensor bleibt erreichbar, Update lässt sich erneut starten
- [ ] **Recovery per Taster** funktioniert weiterhin (Zwangs-DFU)
- [ ] Nach fehlgeschlagenem/abgebrochenem Update: Sensor bootet nicht in eine
      kaputte App (CRC-Prüfung greift), sondern bleibt im Bootloader

---

## D – Abschluss vor dem Release

- [ ] Alle Blöcke ohne offene Befunde
- [ ] Versionsnummern gezogen (Firmware `version.h`, App `pubspec.yaml`,
      Bootloader `BL_VERSION`) – kein `-dev` mehr
- [ ] `RELEASE_NOTES.md` in allen betroffenen Repos ergänzt
- [ ] Tags gesetzt und gepusht
- [ ] Dieser ausgefüllte Testplan committet

---

## Befunde

| Nr. | Block | Beobachtung | Status |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
