# Bedienungsanleitung – Füllstandsensor (LevelSense)

Kurzanleitung zur Bedienung des NMEA2000-Füllstandsensors: LED-Anzeige,
Taster, Kalibrierung sowie Konfiguration über PC-Tool und Handy-App.

## 1. Überblick

Der Sensor misst den Füllstand über einen Drucksensor und gibt ihn auf drei
Wegen aus: über das **NMEA2000-Netzwerk** (z. B. an ein Plotter-Display), über
einen **Analogausgang** (0,5–4,5 V) und über eine **RGB-LED**. Bedient und
kalibriert wird er per **Taster** am Gerät, per **PC-Programm** (PEAK PCAN-USB)
oder per **Handy-App** (Bluetooth).

## 2. LED-Anzeige

**Normalbetrieb:** Die LED zeigt den Füllstand als Farbe, mit langsam
pulsierender Helligkeit:

- **Rot** = leer, Richtung **Blau** = etwa halb, **Grün** = voll.

**Störung:** Bei einem Fehler blinkt die LED zusätzlich auf:

- **Blau blinkend** = Problem auf dem CAN-Bus (Senden nicht möglich)
- **Grün blinkend** = Problem mit dem Drucksensor (I²C)

**Firmware-Update (Bootloader):**

- **Weißes Blinken** = Sensor ist im Update-Modus und wartet
- **Blaues Blinken** = Firmware wird gerade übertragen

## 3. Taster-Bedienung

Der Sensor hat einen Taster. Entscheidend ist, **wie lange** du drückst:

- **Kurz** (etwa 0,2–0,5 s)
- **Lang** (etwa 4 s halten)

| Aktion | Wirkung |
|---|---|
| Kurz im Normalbetrieb | Füllstand als **Blinkcode**: Rot = Hunderter, Grün = Zehner, Blau = Einer, Weiß = 0 (Beispiel 73 % → 7× grün, 3× blau) |
| Lang | **Setup-Modus** ein-/ausschalten |
| Kurz im Setup-Modus | Auswahl umschalten: **Grün** = nichts, **Gelb** = auf 100 % kalibrieren, **Rot** = Kalibrierung zurücksetzen |

Im Setup-Modus: Passiert **10 s** lang nichts, bricht der Sensor selbstständig
ab (ohne etwas zu speichern).

## 4. Kalibrierung am Sensor (100 %)

So legst du fest, welcher Füllstand „voll" (100 %) entspricht:

1. Tank **vollständig füllen** und ein paar Sekunden warten, bis der Messwert
   stabil ist.
2. Taster **~4 s halten** → Setup-Modus, LED blinkt **grün**.
3. **1× kurz drücken** → LED **gelb**.
4. Taster wieder **~4 s halten** → zurück im Normalbetrieb. Der aktuell
   anliegende Druck ist jetzt als 100 % gespeichert.

**Zurücksetzen auf Werkswert:** im Setup-Modus **2× kurz** drücken (LED **rot**),
dann **~4 s halten**.

Die Kalibrierung bleibt dauerhaft erhalten – auch nach Stromausfall und nach
einem Firmware-Update.

## 5. Konfiguration & Tankform (PC-Tool / App)

Fluidtyp, Tankkapazität und Geräteinstanz stellst du über das **PC-Programm**
(`nmea2000_gui.py` / `.exe`) oder die **Handy-App** ein. Beides bietet:

- **Live-Anzeige** von Füllstand und Temperatur
- **Konfiguration**: Fluidtyp, Kapazität (Liter), Instanz
- **100%-Kalibrierung** und Zurücksetzen (wie am Taster, nur per Knopf)
- **Tankform**: Für unregelmäßige Tanks 11 Stützstellen einmessen. Der geführte
  Assistent sagt dir, wie viel du einfüllen sollst, und du übernimmst den
  jeweiligen Füllstand – die Kennlinie wird automatisch berechnet und übertragen.

Die App kann zusätzlich den **Bluetooth-Namen** des Sensors ändern und die
**Firmware aktualisieren** (siehe unten).

## 6. Firmware-Update (drahtlos, über die App)

1. In der App unter „Modul" → **Firmware-Update** die neue `.bin` wählen.
2. Bestätigen. Der Sensor startet in den Bootloader (**weißes Blinken**), die
   App verbindet neu und überträgt (**blaues Blinken**, mit Fortschrittsbalken).
3. Am Ende prüft der Sensor die Übertragung und startet in die neue Firmware.

Wird das Update unterbrochen (Abbruch, Stromausfall), bleibt der Sensor sicher
im Bootloader (weißes Blinken) und das Update kann einfach erneut gestartet
werden. Deine Kalibrierung und Einstellungen bleiben dabei erhalten.

### Notfall-Update (erzwungener Bootloader)

Sollte einmal eine Firmware laufen, die sich nicht mehr normal aktualisieren
lässt, kannst du den Bootloader **erzwingen**: Sensor stromlos machen, den
**Taster gedrückt halten** und dabei die Stromversorgung einschalten. Der Sensor
bleibt dann im Bootloader (weißes Blinken) und ist per App aktualisierbar –
auch wenn eine (unbrauchbare) App installiert ist. Nichts wird dabei gelöscht;
lässt du den Taster los und startest neu, läuft die alte Firmware normal weiter.

## 7. Kurzreferenz

| Ich möchte … | So geht's |
|---|---|
| Füllstand grob ablesen | Kurz drücken → Blinkcode |
| Auf 100 % kalibrieren | Lang → kurz (gelb) → lang |
| Kalibrierung zurücksetzen | Lang → 2× kurz (rot) → lang |
| Fluidtyp/Kapazität setzen | PC-Tool oder App |
| Unregelmäßigen Tank einmessen | App → Tankform-Assistent |
| Firmware aktualisieren | App → Firmware-Update |
| Notfall-Update erzwingen | Taster halten **beim Einschalten** |
