## Firmware 2.2.1

**Werksreset ueber den Taster.** Der Setup-Modus hat einen vierten Schritt
bekommen. Die Reihenfolge ist jetzt gruen (nichts), gelb (100-%-Kalibrierung),
blau (Kalibrierung auf Werkswerte - das war bisher rot) und rot (kompletter
Werksreset wie in der App: Kalibrierung, Kennlinie, Name, Instanz und PIN).
Der rote Schritt fragt nach: die LED blinkt danach 5 Sekunden schnell rot und
will einen weiteren kurzen Druck sehen, sonst passiert nichts. Ist der Reset
gelaufen, quittiert die LED mit rot, gruen, blau, weiss kurz hintereinander -
und zwar bei jedem Werksreset, auch bei dem aus der App und dem vom Bus. Ohne
diese Rueckmeldung bleibt offen, ob er ausgefuehrt wurde: Funk und Bus sind im
selben Moment weg, die LED ist das einzige, was noch reden kann. Damit gibt es
einen Ausweg aus einer verkorksten Kopplung, der ohne Bus und ohne Handy
auskommt - der Grund, warum die Firmware sich das automatische Loeschen
sparen kann. Nach so einem Reset haelt das Handy noch die alte Kopplung; die
App merkt das und bietet an, sie zu erneuern.

## Firmware 2.2.0

Die Firmware loescht keine Kopplungen mehr von sich aus.

- **Die Bond-Selbstheilung ist ausgebaut.** Bis 2.1.0 hat die Firmware nach
  drei Verbindungen in Folge, die nie verschluesselt wurden, alle Bonds im
  Funkmodul geloescht und das Modul neu gestartet. Das war die Kompensation
  fuer einen Fehler, den es nicht mehr gibt: Daten in den noch
  unverschluesselten Kanal zu senden, was das Modul zum Neustart brachte und
  die gerade entstehende Kopplung zerlegte. Seit dieser Sendesperre ist die
  Heilung ohne Aufgabe - geblieben waere nur ein Mechanismus, der von sich
  aus Kopplungen zerstoert, auch die von Handys, die nichts falsch gemacht
  haben.
- **Bonds loescht jetzt ausschliesslich der Werksreset.** Bewusst und vom
  Nutzer ausgeloest, nicht mehr im Hintergrund.
- **Erkannt wird die Sackgasse weiter.** Zaehler und Bedingungen bleiben; im
  Langzeit-Protokoll steht dann das neue Ereignis "Pairing-Sackgasse
  erkannt" statt der ausgeloesten Heilung. Die Diagnose verliert also
  nichts, nur das Eingreifen entfaellt. Das Werkzeug zeigt den Zaehler ab
  Version 1.2.1 als "Sackgassen erkannt" an.
- Auf der Gegenseite raeumt die App auf - ab App 2.2.0 fragt sie den Nutzer,
  bevor sie die Kopplung erneuert, statt sie still zu loeschen.

## Firmware 2.1.0

Nur Diagnose: die Firmware zeichnet jetzt auf, was mit den Kopplungen
passiert. An der Funktion aendert sich nichts.

- **Langzeit-Protokoll des BLE-Zweigs.** Das bisherige Ereignisprotokoll
  fasst 24 Eintraege und ist im Normalbetrieb nach Minuten
  ueberschrieben; sein Zeitstempel laeuft nach 109 Minuten um. Fuer
  einen Fehler, der erst nach Stunden auftritt, taugt es damit nicht.
  Daneben laeuft jetzt ein zweites, grobes Protokoll mit 24 Plaetzen,
  in das nur die seltenen Ereignisse gehen: Neustart des
  Mikrocontrollers, Neustart des Funkmoduls, Kopplung ohne bekannten
  Bond, Fehlermeldung des Moduls, geloeschte Bonds, Modul-Reset, jede
  ausgeloeste Selbstheilung und jede Verbindung, die unverschluesselt
  endete. Zeitstempel in Sekunden seit dem Start, 32 Bit - der laeuft
  nicht ueber.
- **Zaehler seit dem Start.** Verbindungen insgesamt, davon
  verschluesselt beendet, ohne Verschluesselung beendet,
  Selbstheilungen samt Zeitpunkt der letzten, Neustarts des Funkmoduls.
  Sie laufen unabhaengig vom Ringpuffer weiter, bleiben also aussagefaehig,
  wenn der laengst umgelaufen ist.
- Abgefragt wird beides ueber NMEA2000 (proprietaeres Kommando 0x08,
  Antwort 0x88), im PC-Werkzeug ab 1.2.0 ueber die Schaltflaeche
  »BLE-Langzeit«. Bewusst ueber den CAN-Bus: gebraucht wird die
  Auskunft genau dann, wenn ueber BLE nichts mehr geht.

## Firmware 2.0.0

Die BLE-Verbindung ist ab Werk mit einer PIN geschuetzt, und der
Drucksensor liefert bei starkem Unterdruck wieder plausible Werte.

### BLE-PIN
- Das Funkmodul verlangt bei der Kopplung eine PIN. Werksseitig ist das
  die Modul-Standard-PIN 123123.
- Die PIN laesst sich per App aendern (`PIN`-Kommando, sechsstellig). Sie
  liegt im Konfigurationsspeicher und ueberlebt einen Neustart; der
  Werksreset stellt 123123 wieder her.
- Nach einer PIN-Aenderung und nach dem Werksreset entfernt die App die
  alte Kopplung selbst, damit das Telefon beim naechsten Verbinden neu
  nach der PIN fragt.
- Die einmalige Provisionierung (Sicherheitsmodus, PIN, Bonds loeschen)
  startet damit, das Funkmodul neu zu starten. Waehrend es bootet, kann
  sich kein Telefon verbinden, und die Kette bekommt ihr erstes Kommando
  sicher los.
- Die Bond-Selbstheilung zaehlt einen Fehlversuch jetzt daran, dass keine
  Verschluesselung zustande kam. Vorher galt schon ein offener Datenkanal
  als Erfolg - den oeffnet das Modul aber vor dem Pairing, womit die
  Heilung nie ansprang.
- Nutzdaten gehen erst ueber eine verschluesselte Verbindung raus. Der
  Datenkanal geht schon auf, waehrend die Kopplung noch laeuft; der
  Statustext lief bisher genau dort hinein und liess das Funkmodul mit
  einer Fehlermeldung neu starten. Fuer das Telefon sah das aus wie ein
  verschwundener Sensor, und die Kopplung kam nie zustande.
- Die Kette fasst das Funkmodul nicht mehr an, solange ein Telefon
  verbunden ist. Jede Aenderung und jeder Reset startet das Modul neu,
  und zwar ohne Trennungsmeldung - das Telefon sah den Sensor dadurch
  mitten im Koppeln verschwinden, und die PIN-Abfrage lief ins Leere.
  Jetzt wartet die Kette, bis die Verbindung zu Ende ist.
- Auf den Neustart des Moduls wartet die Kette nicht mehr nur passiv:
  nach zwei Sekunden fragt sie aktiv nach. Blieb die Startmeldung des
  Moduls aus, lief vorher jeder Schritt in den Timeout und die Kette gab
  auf - PIN und Bindungen blieben ungeschrieben, das Telefon fragte nach
  der PIN und das Modul pruefte gegen eine andere.
- Sollten Telefon und Sensor unterschiedlicher Meinung darueber sein, ob
  eine Kopplung besteht, loest sich das von allein: nach drei
  Verbindungen, in denen weder ein Kanal noch ein Pairing zustande kam,
  loescht die Firmware ihre Bindungen einmalig (hoechstens dreimal je
  Start). Details in `BLE_Protokoll.md`.
- Neu: eine BLE-Diagnose ueber den CAN-Bus (proprietaeres Kommando 0x06 auf
  PGN 126720, Schaltflaeche im PC-Programm `LevelSense-NMEA2000`). Sie
  meldet den Zustand der Provisionierung, die aus dem Funkmodul
  zurueckgelesenen Sicherheitseinstellungen samt Modul-Firmware und ein
  Protokoll der letzten 24 Ereignisse zwischen STM32 und Funkmodul. Damit
  laesst sich eine gescheiterte Kopplung ueberhaupt erst zuordnen: Modul
  hat getrennt, Modul ist neu gestartet, STM32 ist neu gestartet (mit
  Grund aus `RCC->CSR`) oder keiner von beiden hat etwas getan. Der Weg
  ueber den Bus ist bewusst gewaehlt - er funktioniert genau dann, wenn
  ueber BLE nichts mehr geht.

### Unterdruck jenseits des Messbereichs
- Der DSP des WSEN-PDMS rechnet bei Unterdruck unterhalb des
  Nennbereichs unter null weiter, das Registerfeld ist aber
  vorzeichenlos: aus einem kleinen negativen Rohwert wird eine sehr
  grosse Zahl. Die Begrenzung in `sensor_scale.h` hat die als
  Ueberschreitung nach oben gelesen - der Sensor sprang von vollem
  Unterdruck auf vollen Ueberdruck (Variante 1003 bei etwa -12,5 mbar,
  Variante 1001 bei etwa -125 mbar).
- Gerechnet wird jetzt mit dem vorzeichenbehafteten Abstand zur
  Bereichsmitte. Der Umlauf wird damit erkannt, und der Wert bleibt an
  der Untergrenze stehen, wo er hingehoert. Richtig ist das bis zum
  rund 2,5-fachen Vollausschlag in beide Richtungen.
- Darueber hinaus reicht ein einzelner Messwert nicht mehr aus: bei
  Rohwert 49152 laeuft das Register von -32768 auf +32767 digits um, und
  beide Richtungen sehen dann gleich aus. Am Aufbau gemessen ist das
  Variante 1003 bei etwa -25 mbar; die Firmware meldete ab dort wieder
  vollen Ueberdruck und 99,5 % Fuellhoehe.
- Die Firmware merkt sich deshalb, durch welche Grenze der Messwert den
  Nennbereich verlassen hat, und bleibt an dieser Grenze stehen, bis er
  zurueckkommt. Ausserhalb des Bereichs ist der Betrag ohnehin ohne
  Aussage - gebraucht wird nur die Richtung, und die kann sich nicht
  aendern, ohne dass der Sensor den Nennbereich durchlaufen haette.
  Einzige Luecke: startet die Firmware, waehrend der Druck schon jenseits
  des Umlaufs liegt, fehlt die Vorgeschichte. Das gibt sich, sobald der
  Druck einmal im Nennbereich war.
- Die Host-Tests pruefen den Umschlagpunkt, laufen die Kennlinie ueber
  alle 65536 Rohwerte auf Monotonie ab und fahren die am Aufbau
  gemessene Messreihe durch den Umlauf hindurch nach.
- Neu: eine Rohwert-Diagnose ueber den CAN-Bus (proprietaeres Kommando
  0x07 auf PGN 126720, Schaltflaeche im PC-Programm
  `LevelSense-NMEA2000`). Sie liefert die ganze Rechenkette einer Messung
  einzeln - Registerwert, Abstand zur Bereichsmitte, uBar vor und nach
  Offset, gefilterten Druck, Temperatur und Prozent vor und nach der
  Linearisierung. Ueber die App ist das nicht zu sehen: die zeigt nur das
  Ende der Kette, und das steht bei Unterlast auf dem begrenzten Wert.
  Damit war nicht zu sagen, an welcher Stelle ein Wert umschlaegt. Genau
  damit ist der Umschlagpunkt oben dann gefunden worden.

## Firmware 1.2.11

Die Hardware-Kennung steht jetzt im OTP des Controllers, und die Platine V1
wird wieder aus dem Hauptzweig bedient.

### Hardware-Kennung im OTP
- Variante und Platinen-Buchstabe werden bei der Produktion in den
  OTP-Bereich des STM32G0B1 geschrieben (ab 0x1FFF7000). Die Kennung ist
  damit eine Eigenschaft des Geraets und nicht mehr nur eine Behauptung
  der aufgespielten Firmware.
- Beim Start vergleicht die Firmware den OTP-Eintrag mit der Variante,
  fuer die sie gebaut wurde. Weichen beide voneinander ab, stellt sie das
  Senden auf NMEA2000 ein. Eine versehentlich aufgespielte Fremdvariante
  meldet damit gar nichts mehr, statt still einen um Faktor zehn falschen
  Fuellstand zu liefern.
- Die `STAT`-Zeile meldet `HWV` aus dem OTP, sobald das Geraet
  provisioniert ist; sonst weiterhin aus dem Build.
- Der OTP laesst sich je Doppelwort nur ein einziges Mal beschreiben,
  auch derselbe Wert darf nicht nachgeschrieben werden. Deshalb eine
  Liste aus 16 Slots: eine Korrektur ist ein neuer Eintrag dahinter, es
  gilt immer der letzte gueltige. Details in Issue #2.

### Variante 1000 wieder im Hauptzweig
- Die Platine V1 (STM32G0B1KBU6N) bekommt wieder Firmware aus dem
  Hauptzweig, das Release enthaelt also erneut ein `_hwv1000.bin`. Die
  V1-Linie war mit 1.2.9 eingefroren; ihr fehlte damit unter anderem die
  Behebung des NMEA2000-Empfangsausfalls aus 1.2.10 (Issue #1).
- Dafuer war keine Aenderung an der Hardware noetig. Die Pinbelegungen
  von KBU6 und KBU6N ueberschneiden sich fast nicht, deshalb darf die von
  CubeMX erzeugte Init unveraendert durchlaufen - sie schreibt auf der
  jeweils anderen Platine in Pads, die es physisch nicht gibt.
  Nachgezogen wird nur, was wirklich gebraucht wird.
- Auf der Platine V1 liegt CAN auf PD0/PD1 statt auf PA11/PA12 und
  CAN_STBY auf PD2 statt auf PB4. Das erledigt `board_pins.c`, aufgerufen
  aus den von CubeMX geschuetzten USER-CODE-Bloecken - eine
  Neugenerierung des Projekts bleibt damit erlaubt.

### Intern
- Die CI baut alle drei Varianten jetzt bei jedem Push mit derselben
  Toolchain wie das Release, nicht mehr nur beim Freigabe-Tag. Ein Fehler
  in einer einzelnen Variante faellt damit nicht mehr erst bei der
  Freigabe auf. Die Binaries haengen als Artefakt am Lauf.
- Das CubeIDE-Projekt hat Build-Konfigurationen fuer alle drei Varianten
  (`Debug_1000`, `Debug_1001`, `Debug_1003`). Vorher waren Variante und
  Include-Pfad fest verdrahtet, ein Build aus der IDE lieferte immer 1003.

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
