# Inbetriebnahme CAN-Füllstandsensor

## 1. Anschluss

Sensor an den NMEA2000-Bus anschließen (250 kbit/s, Bus an beiden Enden terminiert) und versorgen. Die LED pulsiert im Messmodus farbig (rot = leer → blau → grün = voll). Zur Kontrolle am PC: PCAN-USB anstecken und `nmea2000_gui.py` starten → „Verbinden" – Füllstand und Temperatur müssen erscheinen.

## 2. Maximalwert kalibrieren (100 %)

Zuerst den Tank **komplett füllen** (auf den Stand, der 100 % sein soll) und ein paar Sekunden warten, bis der Messwert stabil ist. Dann auf eine der beiden Arten kalibrieren:

**Variante A – am PC (empfohlen):** In der GUI auf „Kalibrierung…" klicken und im Bereich „Maximalwert (100 %)" auf „Als 100 % setzen". Der aktuelle Füllstand wird angezeigt; im Log erscheint die Bestätigung „100%-Kalibrierung: übernommen".

**Variante B – am Gerät per Taster:**

1. Taster **~4 s halten** → Setup-Modus, LED blinkt **grün**.
2. **1× kurz drücken** (ca. 0,3 s) → LED **gelb**.
3. Taster wieder **~4 s halten** → zurück im Messmodus, der aktuelle Druck ist als 100 % gespeichert.

Zurücksetzen auf Werkskalibrierung: in der GUI „Zurücksetzen", oder am Taster im Setup **2× kurz** (LED **rot**), dann lang drücken. Hinweis zum Taster: 10 s ohne Tastendruck im Setup = Abbruch ohne Speichern.

## 3. Grundkonfiguration (GUI)

Im Bereich **„Sensor konfigurieren"**: Fluidtyp, Tankkapazität in Litern und Instanz setzen → **Senden**. Im Log muss „GF-Acknowledge … OK" erscheinen. Die Werte bleiben dauerhaft gespeichert. Die Kapazität wird zusätzlich für die Litereingabe im Kalibrier-Fenster gebraucht – sie also vor Schritt 4 setzen.

## 4. Tankform einstellen (nur bei unregelmäßigem Tank)

Bei gleichmäßigem Tank (stehender Quader/Zylinder) diesen Schritt überspringen – die Standardtabelle passt.

Sonst im Fenster **„Kalibrierung…" → „Tankform – Schritt für Schritt einmessen"** vorgehen. Die Software gibt die Füllmengen vor und rechnet die Kennlinie selbst aus:

1. **„Messung vorbereiten"** klicken – setzt die Kennlinie auf linear, damit der Sensor die rohe Füllhöhe anzeigt.
2. Tank **leeren** und in der Zeile „Tank leeren" den Füllstand übernehmen (sollte ~0 % sein).
3. Zeile für Zeile die **angegebene Menge einfüllen** (die Software sagt „+X L", z. B. je ein Zehntel der Kapazität), kurz warten bis der Wert stabil ist, dann rechts **„Übernehmen"** klicken (oder den Füllstand von Hand eintragen).
4. Wenn alle Zeilen ausgefüllt sind: **„Tabelle berechnen & senden"** → im Log erscheint „Stützstellen-Tabelle: gespeichert". Ab jetzt zeigt der Sensor **Volumen-%** statt Füllhöhe an – auf dem Plotter, am Analogausgang und an der LED.

Der abgelesene Füllstand muss von Zeile zu Zeile steigen; sinkende Werte lehnt die Software ab.

## Kurzreferenz Taster

| Aktion | Wirkung |
|---|---|
| kurz (~0,3 s) im Messmodus | Füllstand als Blinkcode (rot = 100er, grün = 10er, blau = 1er, weiß = 0) |
| lang (~4 s) | Setup-Modus rein/raus |
| kurz im Setup | Auswahl: grün = nichts, gelb = 100 % kalibrieren, rot = Kalibrierung zurücksetzen |
