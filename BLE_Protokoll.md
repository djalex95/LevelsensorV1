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

Ab Firmware 2.0.0 ist die BLE-Schnittstelle mit **Static-Passkey-Pairing +
Bonding** gesichert: beim ersten Verbinden fragt das Betriebssystem die
6-stellige Kopplungs-PIN ab (Werks-PIN **123123**, per `PIN`-Kommando
änderbar). Danach ist das Gerät gekoppelt und verbindet ohne erneute
Eingabe. Firmware bis 1.3.0 läuft unverschlüsselt (siehe Hinweis unten).

Danach: Kommandos als UTF-8/ASCII-Text in RX schreiben, Antworten und den
periodischen Status als TX-Notification empfangen. Jede Nachricht endet mit `\n`.

## 2. Sensor → App

### Status (automatisch ca. jede Sekunde, solange verbunden)

```
STAT;L=73.5;T=23.45;F=1;C=150;I=0;CAL=1;V=1.3.0;HWV=1003A;E=0;P=68500
```

Die App zerlegt die Zeile in `Schlüssel=Wert`-Paare; unbekannte oder
fehlende Felder werden toleriert (vorwärts-/rückwärtskompatibel).

| Feld | Bedeutung | Einheit / Wertebereich |
|---|---|---|
| `L` | Füllstand | Prozent, eine Nachkommastelle |
| `T` | Temperatur | Grad C, zwei Nachkommastellen (Vorzeichen möglich) |
| `F` | Fluidtyp | 0..15 (siehe Tabelle unten) |
| `C` | Tankkapazität | Liter |
| `I` | Geräteinstanz | 0..15 |
| `CAL` | 100%-Kalibrierung vorhanden | 0 oder 1 |
| `V` | Firmware-Version | z. B. `1.3.0` |
| `HWV` | Hardware-Variante + Platinen-Buchstabe | z. B. `1003A` (`1000`=Druck V1, `1001`=Druck V2 ±10 kPa, `1003`=Druck V2 flach ±1 kPa) |
| `E` | Fehlerbits (1=CAN, 2=I2C, 4=Hardware-Variante) | 0 = kein Fehler |
| `P` | Messdruck, ungefiltert und offsetkorrigiert | µBar (auch negativ möglich) |

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
OK CAL0 1234      Nullpunkt kalibriert (mit neuem Offset in µBar)
OK CAL0RESET      Nullpunkt auf Werkswert zurückgesetzt
OK CALRESET       100%-Kalibrierung zurückgesetzt (Nullpunkt bleibt)
OK FILT 900       Filterstärke gesetzt
OK PIN            Kopplungs-PIN geändert (Sensor startet neu, neu koppeln)
ERR CAP           Wert ungültig
ERR LIN           Kennlinie ungültig (nicht 0..100 oder nicht steigend)
ERR CAL100 nodruck  Kalibrierung nicht möglich (kein Druck anliegend)
ERR CAL0 range    Nullpunkt außerhalb ±30 mBar (Sensor prüfen)
ERR FILT          Filterwert ungültig (erlaubt: 0..990)
ERR PIN           PIN ungültig (genau 6 Ziffern nötig)
ERR BONDS         Bond-Tabelle nicht lesbar
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
| `CAL` | Kalibrierwerte abfragen (für die Sicherung) | `CAL;<0/1>;<max_val>;<offset>` |
| `CAL n[,m]` | Kalibrierwerte direkt setzen (Wiederherstellung; n = max_val 1..1000000, m = Offset ±30000, optional) | `OK CAL n` / `ERR CAL` |
| `CAL0` | aktuellen Druck als Nullpunkt übernehmen (**Tank leer!**) | `OK CAL0 m` (m = neuer Offset) / `ERR CAL0 range` |
| `CAL0RESET` | nur den Nullpunkt auf Werkswert zurücksetzen (100 % bleibt) | `OK CAL0RESET` |
| `CAL100` | aktuellen Füllstand als 100 % kalibrieren | `OK CAL100` / `ERR CAL100 nodruck` |
| `CALRESET` | 100%-Kalibrierung auf Werkswert zurücksetzen (Nullpunkt bleibt) | `OK CALRESET` |
| `FILT` | Filterstärke abfragen | `FILT;<0..990>` |
| `FILT n` | Filterstärke setzen: Anteil des alten Werts in Promille (0 = aus, 900 ≈ 1 s Zeitkonstante bei 100 ms Messtakt) | `OK FILT n` / `ERR FILT` |
| `PIN dddddd` | Kopplungs-PIN ändern (genau 6 Ziffern). Der **Sensor startet neu** und provisioniert das Funkmodul beim Boot mit der neuen PIN (löscht dabei alle Kopplungen) – jedes Gerät koppelt sich beim nächsten Verbinden mit der neuen PIN neu | `OK PIN`, dann Neustart |
| `BONDS` | Diagnose: Anzahl der im Modul gespeicherten Kopplungen, letzter Security-Status (0 = wiedererkannt, 1 = neu gekoppelt, 255 = noch keiner) sowie die Modul-Firmware-Version als zweite Zeile `MODFW;x.y.z`. `ERR BONDS st=255` = Kommando von dieser Modul-Firmware nicht unterstützt | `BONDS;<n>;SEC=<s>` + `MODFW;…` / `ERR BONDS …` |
| `FACTORYRESET` | Werksreset: löscht Kalibrierung (100 % + Nullpunkt), Glättung, Tankform, Instanz, Name, gespeicherte Adresse sowie Kopplungs-PIN und alle Kopplungen; Sensor startet neu (BLE-Name wieder `LevelSense-<UID>`, PIN wieder 123123) | `OK FACTORYRESET`, dann Neustart |
| `NAME text` | BLE-Modulnamen dauerhaft ändern (max. 20 Zeichen) | `OK NAME`, danach **startet das Modul neu** und die Verbindung trennt sich |
| `DFU` | in den Firmware-Update-Modus wechseln | `OK DFU`, dann Neustart → Bootloader (siehe `../Bootloader/DESIGN.md`) |

**Hinweis zu `CAL`:** `max_val` ist der Rohdruck bei 100 % Füllstand – genau
der Wert, den `CAL100` aus dem anliegenden Druck bildet; `offset` ist der
Nullpunkt aus `CAL0` (µBar, wird vom Sensortreiber direkt vom Messwert
abgezogen). Mit der Abfrage lässt sich die Kalibrierung sichern und mit
`CAL n,m` später ohne vollen Tank wiederherstellen, etwa nach einem
Gerätetausch. Ist der Sensor unkalibriert, meldet die Abfrage `CAL;0;…`.

**Kalibrier-Reihenfolge:** erst bei leerem Tank `CAL0` (Nullpunkt), dann bei
vollem Tank `CAL100`. Der Nullpunkt geht in den Messwert ein, aus dem `CAL100`
seinen `max_val` bildet – andersherum stimmt die 100%-Marke danach nicht mehr.
Zur Kontrolle zeigt das Feld `P` der `STAT`-Zeile den offsetkorrigierten,
ungefilterten Druck: bei leerem Tank nach `CAL0` steht dort ~0.

Beide Kommandos übernehmen den **ungefilterten** Messwert – also genau den
Wert aus dem Feld `P`, unabhängig von der eingestellten Glättung (`FILT`).
Die Glättung wirkt nur auf den laufenden Füllstand, nicht auf die
Kalibrierung. Da `max_val` Tankhöhe, Flüssigkeitsdichte und
Sensorverstärkung zusammenfasst, gilt ein übertragener Wert am selben Tank als
sehr guter Startwert, ersetzt aber bei hohem Genauigkeitsanspruch keine echte
Nachkalibrierung.

**Sicherung der Konfiguration:** Eine vollständige Sicherung besteht aus
`CAL` (Kalibrierung inkl. Nullpunkt), `LIN` (Tankform), `FILT` (Filterstärke),
`NAME` (Name) sowie `F`, `C` und `I` aus der `STAT`-Zeile. Beim
Wiederherstellen werden `LIN …`, `FLUID …`, `CAP …`, `INST …`, `NAME …`,
`FILT …` und `CAL …` gesendet. Bewusst **nicht** Teil der
Sicherung sind die geclaimte NMEA2000-Quelladresse (Byte 30 – würde auf dem
Bus zu Adresskonflikten führen; sie wird beim Start ohnehin neu ausgehandelt)
und der interne Provisioning-Marker. Die DAC-Kalibrierung liegt fest in der
Firmware und wird nicht im Config gespeichert.

**Hinweis zu `NAME`:** Der Name wird zusätzlich persistent im Sensor
gespeichert und als *Installation Description 1* in PGN 126998
(Configuration Information) auf dem NMEA2000-Bus gemeldet – er erscheint
damit auch in der Geräteliste des Plotters und kann von dort per
Group Function (126208 → 126998, Feld 1) geändert werden.

Der Modulname wird über den Proteus-Befehl `CMD_SET_REQ` (Settings-Index 2,
`RF_DeviceName`) im Modul-Flash gespeichert und bleibt nach einem Neustart
erhalten. Nach `NAME …` muss neu verbunden werden.

**Namensabgleich:** Der im Sensor gespeicherte Name ist die einzige Quelle
der Wahrheit. Bei jedem Boot liest die Firmware den Modulnamen aus
(`CMD_GET_REQ`) und schreibt ihn **nur bei Abweichung** neu – so wird das
Modul-Flash geschont und Umbenennungen vom Plotter (Group Function) landen
spätestens beim nächsten Boot auch im BLE-Namen; zur Laufzeit werden sie
sofort übernommen. Ist kein Name gesetzt (Werkszustand bzw. nach
`FACTORYRESET`), heißt der Sensor `LevelSense-<UID>` – die UID ist die
NMEA2000 Unique Number der Platine, damit sind mehrere fabrikneue Sensoren
unterscheidbar. Wird `NAME` mit dem bereits gesetzten Namen erneut gesendet,
antwortet der Sensor nur mit `OK NAME` (kein Modul-Neustart, Verbindung
bleibt bestehen). Hinweis: Der Sensor speichert max. 24 Zeichen, das
BLE-Modul zeigt davon max. 20.

**Sicherheit:** Ab Firmware 2.0.0 läuft die Schnittstelle mit
`RF_SecFlags = 0x0B` (Static Passkey + Bonding). Die Provisionierung passiert
einmalig nach Update/Werksreset als bestätigte Schritt-Kette: Modul neu
starten → SecFlags schreiben → Modul-Neustart abwarten → Passkey schreiben →
Neustart abwarten → Bonds löschen → Neustart. Der Neustart am Anfang ist kein
Schönheitsfehler: solange das Modul bootet, kann sich kein Telefon verbinden,
und `CMD_SET_REQ` braucht den getrennten Zustand.

Zwei Eigenschaften der Kette sind dabei entscheidend. **Erstens** fasst sie das
Modul nie an, solange eine Verbindung besteht – auch nicht für den einleitenden
Neustart. Jedes `CMD_SET_REQ` und jeder Reset startet den Proteus-e neu, und
zwar ohne Trennungsmeldung: das Telefon sieht den Sensor mitten im Pairing
verschwinden (`LINK_SUPERVISION_TIMEOUT`) und die Kopplung kommt nie zustande.
Die Kette wartet deshalb ab; ihr Fenster sind die ersten Sekunden nach dem
Einschalten. Bleibt eine Verbindung 30 s lang bestehen, ohne dass der
Datenkanal aufgeht oder gekoppelt wird, trennt die Firmware sie einmal sauber
per `CMD_DISCONNECT_REQ` – ein normales Verbindungsende, kein Reset.
**Zweitens** wartet sie auf den Modul-Neustart nicht nur passiv: nach zwei
Sekunden fragt sie zusätzlich aktiv mit `CMD_GETSTATE_REQ` nach. Die Antwort
`CMD_GETSTATE_CNF` ist dieselbe Meldung, die das Modul nach einem Neustart von
sich aus schickt. Ginge die spontane Meldung verloren, liefe sonst jeder
Schritt in den Timeout, die Kette gäbe auf, und PIN und Bonds blieben
ungeschrieben – bei gesetzten SecFlags heißt das: das Telefon fragt nach der
PIN, aber das Modul prüft gegen eine andere. (Der frühere PIN-Anlauf
scheiterte nicht am Modul –
Bonds liegen persistent im Modul-Flash –, sondern an zwei Firmware-Fehlern:
die PIN wurde anfangs bei jedem Boot neu geschrieben samt Bond-Löschung, und
die Provisionierung wartete die Selbst-Neustarts des Moduls nach `CMD_SET_REQ`
nicht ab, wodurch Folgekommandos verloren gingen.) Bleibt ein Gerät nach
PIN-Wechsel oder Werksreset beim Koppeln hängen, dort in den
Bluetooth-Einstellungen den Sensor entfernen – die App erledigt das auf
Android automatisch (Bond-Selbstheilung).

Die Firmware heilt auch die umgekehrte Sackgasse selbst: hält das **Modul**
noch einen alten Bond für ein Gerät, das seinen eigenen verloren hat, bricht
das Pairing dort vor der PIN-Abfrage ab. Enden drei Verbindungen in Folge,
ohne dass der Datenkanal aufging **und** ohne dass eine Sicherheitsmeldung
(`CMD_SECURITY_IND`) kam, löscht die Firmware die Modul-Bonds und startet das
Modul neu – danach koppelt das Gerät frisch. Kam dagegen eine
Sicherheitsmeldung, war die Kopplung in Ordnung und es wird nichts gelöscht;
sonst würde jeder App-seitige Abbruch einen frisch angelegten Bond wieder
zerstören. Pro Sensorstart sind höchstens drei Heilungen möglich, nach jeder
erfolgreichen Verbindung startet die Zählung neu. Der Neustart am Ende der
Heilung entfällt, wenn sich in der Zwischenzeit wieder ein Gerät verbunden hat
– er würde es mitten im neuen Pairing hinauswerfen und damit genau den Fehler
erzeugen, den die Heilung beheben soll. Wirksam ist ohnehin das Löschen der
Bonds.

Auf App-Seite gehört dazu die Geduld beim Pairing: Das Einschalten der
Notifications (Descriptor 2902) ist der Schreibzugriff, der die PIN-Abfrage
überhaupt auslöst. Android meldet ihn sofort als `GATT_ERROR (133)` zurück,
während der PIN-Dialog erst erscheint. Die App bricht deshalb nicht ab,
sondern wartet den Abschluss der Kopplung ab (bis 60 s) und richtet die
Verbindung danach neu ein.

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
| Nullpunkt-Kalibrierung (Tank leer) | `CAL0`, Kontrolle über `P` aus `STAT` |
| Glättung einstellen (schwappender Tank) | `FILT` lesen, `FILT n` setzen |
| Rohdruck anzeigen (Diagnose) | `P` aus der `STAT`-Zeile |
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

## 6. Diagnose über NMEA2000, wenn die Kopplung scheitert

Scheitert die Kopplung, fehlt genau der Weg, über den man sonst nachsieht:
der BLE-Datenkanal geht ja gerade nicht auf. Deshalb liegt die Diagnose des
BLE-Zweigs auf dem CAN-Bus, nicht auf BLE. Das PC-Programm
`LevelSense-NMEA2000` schickt dazu das proprietäre Kommando `0x06` auf
PGN 126720; der Sensor antwortet mit `0x86`.

Die Antwort enthält drei Dinge:

**Zustand der Provisionierung** – Schritt und Teilschritt der Kette, Zahl der
Anläufe, ob der Marker im Konfigurationsspeicher gesetzt ist, wie oft die
Bond-Heilung schon gegriffen hat und wie viele Verbindungen in Folge ohne
Kanal und ohne Pairing endeten.

**Was tatsächlich im Funkmodul steht** – `RF_SecFlags`, die statische Passkey
und die Firmware-Version des Moduls, einmalig nach dem Start zurückgelesen.
Erst diese drei Werte beantworten die Frage, ob das Modul überhaupt im
erwarteten Modus steht (`0x0B`) und ob die dort hinterlegte PIN die des
Sensors ist. Vorher war das geraten.

**Ein Protokoll der letzten 24 Ereignisse** zwischen STM32 und Funkmodul, je
mit Zeitstempel in Zehntelsekunden seit dem Start. Aufgezeichnet wird jedes
Kommando in beide Richtungen; nur der Nutzdatenverkehr bleibt draußen, weil
er den kleinen Puffer in Sekunden überschreiben würde.

Das Protokoll beantwortet die eine Frage, an der die Fehlersuche bisher
hängen blieb – warum der Sensor mitten im Koppeln verstummt:

- Steht dort `DISCONNECT_IND`, hat das **Modul die Verbindung beendet**, und
  das Byte dahinter nennt den Grund.
- Steht dort `GETSTATE_CNF`, ist das **Modul neu gestartet**. Dann verrät der
  Eintrag davor, ob der STM32 etwas geschickt hat (`SET_REQ`, `RESET_REQ`)
  oder ob das Modul von sich aus neu gestartet ist.
- Steht dort `STM32 gestartet`, hat sich der **STM32 neu gestartet**. Er zieht
  dabei über `MX_GPIO_Init` die Resetleitung des Moduls – von außen sieht das
  genauso aus wie ein Modul-Neustart. Der mitgelieferte Neustartgrund aus
  `RCC->CSR` sagt dann, ob Watchdog, Spannungseinbruch oder ein
  Software-Reset dahinter steckt.
- Steht dort **gar nichts** in dem Moment, hat weder der STM32 etwas gesendet
  noch das Modul etwas gemeldet. Dann liegt es an der Funkstrecke oder am
  Modul selbst, und die Suche gehört auf die Modul-Firmware und die
  Sicherheitseinstellungen gelenkt.

### Langzeit-Protokoll (Kommando `0x08`, Antwort `0x88`)

Das Protokoll oben fasst 24 Einträge und ist im Normalbetrieb nach wenigen
Minuten überschrieben; sein Zeitstempel ist 16 Bit in Zehntelsekunden und läuft
nach 109 Minuten um. Für einen Fehler, der erst nach Stunden auftritt - etwa
eine Kopplung, die über Nacht verschwindet - taugt es damit nicht.

Daneben läuft deshalb ein zweites, grobes Protokoll mit ebenfalls 24 Plätzen.
Es nimmt nur die seltenen Ereignisse auf, dafür mit Zeitstempel in Sekunden
(32 Bit, läuft nicht über):

- `0x01` STM32 gestartet
- `0x02` Funkmodul gestartet
- `0x03` Kopplung ohne bekannten Bond (`0x01` = frisch gekoppelt)
- `0x04` Fehlermeldung des Moduls
- `0x05` Bonds im Modul gelöscht
- `0x06` Modul-Reset gesendet
- `0x07` Bond-Selbstheilung ausgelöst
- `0x08` Verbindung endete unverschlüsselt (Nutzbyte = Fehlerzähler danach)

Dazu Zähler, die unabhängig vom Ringpuffer weiterlaufen: Verbindungen
insgesamt, davon verschlüsselt beendete, ohne Verschlüsselung beendete,
Selbstheilungen samt Laufzeit der letzten und Neustarts des Funkmoduls. Sie
bleiben aussagefähig, wenn der Puffer längst umgelaufen ist.

Damit ist die Frage zu beantworten, wer eine verschwundene Kopplung gelöscht
hat. Steht im Protokoll eine Selbstheilung, war es der Sensor, und die drei
Einträge `0x08` davor zeigen, welche Verbindungen sie ausgelöst haben. Steht
dort nichts, kam es von der Gegenseite - dann führt die Spur ins
Verbindungsprotokoll der App.

## 7. Senden erst über eine verschlüsselte Verbindung

Die erste Messung mit dem Ereignisprotokoll hat den Fehler gefunden, an dem
das Koppeln seit 2.0.0 gescheitert ist. Das Muster wiederholte sich alle zehn
Sekunden identisch:

```
86.0 s  <- CONNECT_IND
86.0 s  <- CHANNELOPEN_RSP
86.0 s  <- ERROR_IND
86.0 s  <- GETSTATE_CNF        (= Modul neu gestartet)
```

`CMD_CHANNELOPEN_RSP` heißt nicht, dass die Verbindung fertig ist. Der Kanal
geht auf, während die Kopplung noch läuft und die Verbindung noch
unverschlüsselt ist. Die Hauptschleife sendete bisher genau dort ihren
Statustext hinein – rund hundert Zeichen, wegen der anfänglichen MPS von 19
Byte in sechs Frames. Das Modul quittiert das mit `CMD_ERROR_IND` und startet
neu. Ein Neustart des Moduls sieht für das Telefon aus wie ein verschwundener
Sensor: keine Trennungsmeldung, nur `LINK_SUPERVISION_TIMEOUT`. Die Kopplung
konnte nie fertig werden, weil der Sensor sie jedes Mal selbst abgeschossen
hat, bevor die PIN überhaupt geprüft war.

Seitdem gilt in `BLE_SendData` eine zweite Bedingung: gesendet wird erst,
wenn der Kanal offen **und** `ble_sec_state != 0xFF` ist, das Modul also
`CMD_SECURITY_IND` gemeldet hat. Der Sicherheitszustand wird dafür in der ISR
bei `CMD_CONNECT_IND` zurückgesetzt, nicht mehr in der Hauptschleife – dort
war es ein Wettlauf, der eine früh eintreffende `CMD_SECURITY_IND` verworfen
hätte. Zusätzlich beginnt der Statustakt mit jeder neuen Verbindung von vorn,
damit das erste Telegramm nicht in dem Moment losgeht, in dem das Telefon
gerade fertig gekoppelt hat.

Im Ereignisprotokoll tauchen dafür zwei neue Einträge auf, jeweils einmal pro
Verbindung: `Senden unterdrueckt` für den ersten abgewiesenen Versuch und
`DATA_REQ` für die ersten tatsächlich abgeschickten Nutzdaten, mit deren
Länge im Nutzbyte. Zusammen zeigen sie in einer Zeile, ob die Reihenfolge
stimmt.
