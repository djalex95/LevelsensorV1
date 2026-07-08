# Bedienungsanleitung – Füllstandsensor (LevelSense)

## 1. Überblick

Misst den Füllstand und gibt ihn über **NMEA2000**, einen **Analogausgang**
(0,5–4,5 V) und eine **RGB-LED** aus. Bedienung und Kalibrierung per **Taster**,
**PC-Programm** (PEAK PCAN-USB) oder **Handy-App** (Bluetooth).

## 2. LED-Anzeige

**Normalbetrieb** – Farbe zeigt den Füllstand, langsam pulsierend:

- **Rot** = leer · **Blau** = halb · **Grün** = voll

**Störung** (LED blinkt):

- **Grün blinkt** = Drucksensor-Problem (I²C)

**Bootloader / Firmware-Update:**

- **Weiß blinkt** = Update-Modus, wartet
- **Blau blinkt** = Firmware wird übertragen

## 3. Taster

Kurz ≈ 0,2–0,5 s · Lang ≈ 4 s halten.

- **Kurz** (Normalbetrieb): Füllstand als Blinkcode (Rot = Hunderter, Grün = Zehner, Blau = Einer, Weiß = 0; 73 % → 7× grün, 3× blau)
- **Lang**: Setup-Modus ein/aus
- **Kurz** (Setup): Auswahl – Grün = nichts, Gelb = 100 % kalibrieren, Rot = Reset

Im Setup ohne Eingabe: nach **10 s** Abbruch (nichts gespeichert).

## 4. Kalibrierung am Sensor (100 %)

Tank voll füllen, kurz warten, dann: **Lang** (grün) → **1× kurz** (gelb) →
**Lang**. Reset: im Setup **2× kurz** (rot) → **Lang**. Die Kalibrierung bleibt
erhalten – auch nach Stromausfall und Firmware-Update.

## 5. Konfiguration & Tankform (PC-Tool / App)

Live-Anzeige, Fluidtyp/Kapazität/Instanz, 100%-Kalibrierung und Reset, Tankform
(11 Stützstellen per Assistent). Die App kann zusätzlich den Bluetooth-Namen
ändern und die Firmware aktualisieren.

## 6. Firmware-Update (über die App)

App → **Modul → Firmware-Update**, Version wählen, bestätigen. Der Sensor startet
in den Bootloader (**weiß**), überträgt (**blau**), prüft und startet neu. Bei
Abbruch bleibt er sicher im Bootloader und ist erneut startbar; die Kalibrierung
bleibt erhalten. **Notfall:** Taster **beim Einschalten** halten erzwingt den
Bootloader.

## 7. Kurzreferenz

| Ich möchte … | So geht's |
|---|---|
| Füllstand grob ablesen | Kurz drücken → Blinkcode |
| Auf 100 % kalibrieren | Lang → kurz (gelb) → lang |
| Kalibrierung zurücksetzen | Lang → 2× kurz (rot) → lang |
| Fluidtyp/Kapazität setzen | PC-Tool oder App |
| Tank einmessen | App → Tankform-Assistent |
| Firmware aktualisieren | App → Firmware-Update |
| Notfall-Update erzwingen | Taster halten beim Einschalten |
