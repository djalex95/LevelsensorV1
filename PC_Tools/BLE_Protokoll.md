# BLE-Protokoll – Füllstandsensor (Proteus-e)

Spezifikation für eine eigene Handy-App (z. B. Flutter). Die App kommuniziert
über Bluetooth LE mit dem Würth Proteus-e-Modul; die Nutzdaten sind einfache
Textzeilen. Das Proteus-UART-Protokoll (Modul ↔ STM32) ist für die App **nicht**
relevant – es wird vom Modul transparent überbrückt.

## 1. GATT-Profil (SPP-like, von Würth vorgegeben)

| Element | UUID |
|---|---|
| Primary Service | `6E400001-C352-11E5-953D-0002A5D5C51B` |
| RX (App → Sensor, Write) | `6E400002-C352-11E5-953D-0002A5D5C51B` |
| TX (Sensor → App, Notify) | `6E400003-C352-11E5-953D-0002A5D5C51B` |

Ablauf beim Verbinden:

1. Nach Gerätename oder Service-UUID scannen und verbinden.
2. **Große MTU anfordern** (bis 247 Byte) – wichtig, damit längere Kommandos
   wie `LIN …` in einem Write passen.
3. Notifications der TX-Charakteristik aktivieren.
4. Ggf. Pairing/Bonding, falls das Modul mit Sicherheit konfiguriert ist.

Danach: Kommandos als UTF-8/ASCII-Text in RX schreiben, Antworten und den
periodischen Status als TX-Notification empfangen. Jede Nachricht endet mit `\n`.

## 2. Sensor → App

### Status (automatisch ca. jede Sekunde, solange verbunden)

```
STAT;L=73.5;T=23.45;F=1;C=150;I=0;CAL=1;V=1.2.0;HW=1000
```

| Feld | Bedeutung | Einheit / Wertebereich |
|---|---|---|
| `L` | Füllstand | Prozent, eine Nachkommastelle |
| `T` | Temperatur | Grad C, zwei Nachkommastellen (Vorzeichen möglich) |
| `F` | Fluidtyp | 0..15 (siehe Tabelle unten) |
| `C` | Tankkapazität | Liter |
| `I` | Geräteinstanz | 0..15 |
| `CAL` | 100%-Kalibrierung vorhanden | 0 oder 1 |
| `V` | Firmware-Version | z. B. `1.2.0` |
| `HW` | Hardware-Revision | 4-stellig, z. B. `1000` |

Damit hat die App aus dem Stream bereits alle Anzeige- und Konfigurationswerte.

### Kennlinie (Antwort auf `LIN`)

```
LIN;0,10,20,30,40,50,60,70,80,90,100
```

11 Werte: Volumen-% bei Füllhöhe 0, 10, 20 … 100 %.

### Antworten auf Kommandos

```
OK CAP 150        Erfolg (mit Echo des gesetzten Werts)
OK LIN            Kennlinie übernommen
OK CAL100         100 % kalibriert
OK CALRESET       Kalibrierung zurückgesetzt
ERR CAP           Wert ungültig
ERR LIN           Kennlinie ungültig (nicht 0..100 oder nicht steigend)
ERR CAL100 nodruck  Kalibrierung nicht möglich (kein Druck anliegend)
ERR ?             unbekanntes Kommando
```

## 3. App → Sensor (Kommandos, je mit `\n` abschließen)

| Kommando | Wirkung | Antwort |
|---|---|---|
| `VER` | Firmware-Version abfragen | `VER;x.y.z` |
| `GET` | sofort eine `STAT`-Zeile senden | `STAT;…` |
| `LIN` | aktuelle Kennlinie abfragen | `LIN;…` |
| `LIN v0,v1,…,v10` | Kennlinie setzen (11 Werte 0..100, steigend) | `OK LIN` / `ERR LIN` |
| `FLUID n` | Fluidtyp setzen (0..15) | `OK FLUID n` / `ERR FLUID` |
| `CAP n` | Kapazität setzen (1..255 L) | `OK CAP n` / `ERR CAP` |
| `INST n` | Instanz setzen (0..15) | `OK INST n` / `ERR INST` |
| `CAL100` | aktuellen Füllstand als 100 % kalibrieren | `OK CAL100` / `ERR CAL100 nodruck` |
| `CALRESET` | Kalibrierung auf Werkswert zurücksetzen | `OK CALRESET` |
| `FACTORYRESET` | Werksreset: löscht Kalibrierung, Tankform, Instanz, Name und gespeicherte Adresse; Sensor startet neu (BLE-Modulname bleibt) | `OK FACTORYRESET`, dann Neustart |
| `NAME text` | BLE-Modulnamen dauerhaft ändern (max. 20 Zeichen) | `OK NAME`, danach **startet das Modul neu** und die Verbindung trennt sich |
| `DFU` | in den Firmware-Update-Modus wechseln | `OK DFU`, dann Neustart → Bootloader (siehe `../Bootloader/DESIGN.md`) |

**Hinweis zu `NAME`:** Der Name wird zusätzlich persistent im Sensor
gespeichert und als *Installation Description 1* in PGN 126998
(Configuration Information) auf dem NMEA2000-Bus gemeldet – er erscheint
damit auch in der Geräteliste des Plotters und kann von dort per
Group Function (126208 → 126998, Feld 1) geändert werden.

Der Modulname wird über den Proteus-Befehl `CMD_SET_REQ` (Settings-Index 2,
`RF_DeviceName`) im Modul-Flash gespeichert und bleibt nach einem Neustart
erhalten. Nach `NAME …` muss neu verbunden werden.

Im **Bootloader-Modus** (während des OTA-Updates) beantwortet der Bootloader
zusätzlich `VER` mit `BLV;x.y.z` (seiner eigenen Version). Die App fragt das beim
Update ab und zeigt die Bootloader-Version an.

Kommandos sind case-insensitive. Alle gesetzten Werte werden dauerhaft im
Sensor gespeichert (überstehen einen Neustart und Stromausfall).

### Fluidtyp-Codes (NMEA2000)

`0` Kraftstoff · `1` Wasser · `2` Grauwasser · `3` Live Well · `4` Öl ·
`5` Schwarzwasser · `6` Benzin

## 4. So bildest du die PC-Funktionen ab

| PC-Programm | App-Umsetzung |
|---|---|
| Live-Füllstand + Temperatur | `STAT`-Zeilen parsen und anzeigen |
| Verlaufsgraph | `L`-Werte über Zeit puffern |
| Fluidtyp / Kapazität / Instanz setzen | `FLUID` / `CAP` / `INST` |
| Aktuelle Konfig anzeigen | aus `STAT` (F, C, I) |
| 100%-Kalibrierung / Reset | `CAL100` / `CALRESET` |
| Tankform lesen | `LIN` → Kennlinie |
| Tankform-Assistent | `LIN` lesen, Liter→% wie im PC-Tool rechnen, `LIN v0,…,v10` schreiben |

## 5. Flutter-Hinweise

- BLE-Bibliothek: z. B. `flutter_reactive_ble` oder `flutter_blue_plus`.
- Als Vorlage eignet sich die Open-Source-App von Würth
  (`WurthElektronik/Proteus-Connect`, Flutter) – sie enthält bereits Scan,
  Verbindung, Pairing, MTU-Anforderung und die richtigen UUIDs.
- Eingehende Notifications können mehrere Zeilen oder Teile enthalten:
  einen Empfangspuffer führen und an `\n` in einzelne Nachrichten zerlegen.
- Vor `LIN …` sicherstellen, dass die MTU groß genug ist (die Zeile kann
  ~40 Zeichen lang sein).
```
