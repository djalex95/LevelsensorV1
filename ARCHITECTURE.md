# Architektur: Ein Kern, mehrere HW-Varianten

Zielbild für die geplante Zusammenführung von drei Sensorvarianten
(Drucksensor V1, Drucksensor V2, Ultraschall) in eine gemeinsame
Firmware-Basis. Kern-Idee: **geteilter Code lebt genau einmal**, damit ein
Fix (z. B. im BLE-Teil) mit einem einzigen Commit alle Varianten erreicht.
Dies ist ein Planungsdokument – die Umsetzung erfolgt schrittweise (siehe
Migrationsplan) und beginnt sinnvollerweise, *bevor* die zweite Variante
real existiert.

## 1. Warum Mono-Repo statt drei Repos

Kopierter Code driftet auseinander – das sieht man heute schon an
`dfu_common.c`, das in Firmware und Bootloader bit-identisch bleiben muss.
Bei drei Varianten mit kopiertem BLE-/NMEA-Stack müsste jeder Fix mehrfach
committet werden. Git-Submodule lösen das theoretisch, sind für einen
Solo-Entwickler aber fummelig (Detached HEAD, Pointer je Repo nachziehen).

Ein **Mono-Repo** gibt das gewünschte Verhalten: ein Commit im Kern → beim
nächsten Build sind alle Varianten aktualisiert.

## 2. Repo-Struktur

```
/core/          <- von ALLEN Varianten gebaut; hier lebt der geteilte Code
   ble.c/.h            Proteus-e-Treiber (UART-Frames, Reset, Namen)
   ble_app.c/.h        BLE-Kommando-Ebene (ein Fix hier gilt fuer alle)
   nmea2000.c/.h       NMEA2000-Stack
   nmea_app.c/.h       NMEA2000-Handler
   app_config.c/.h     Config-/EEPROM-Helfer
   config_store.c/.h   Robuster Ping-Pong-Konfigspeicher
   app_types.h         Gemeinsame Typen/Konstanten
   version.h           EINE gemeinsame Version (siehe 5.)
   hw_variant.c/.h     Variantenkennung aus OTP lesen/pruefen (siehe 6.)
   dfu_common.c/.h     OTA-CRC/Meta; auch vom Bootloader genutzt
/sensor/        <- Messquellen-Abstraktion + Implementierungen
   sensor.h            Gemeinsames Interface (init/get_value/level)
   sensor_pressure_v1.c
   sensor_pressure_v2.c
   sensor_ultrasonic.c
/variants/      <- nur das, was sich WIRKLICH unterscheidet
   pressure_v1/        Board-Init/Pins (main.c-Anteil), .ioc, Linkerscript,
                       Build-Config, HW_VARIANT-Define
   pressure_v2/
   ultrasonic/
/bootloader/    <- variantenunabhaengig; nutzt /core/dfu_common.c direkt
/tools/         <- make_app_meta_hex.py, stamp_bin.py, Provisionierung
```

Die App (Flutter) bleibt ein **eigenes Repo** – sie ist variantenneutral,
weil das BLE-Protokoll aus `/core/ble_app.c` fuer alle Varianten gleich ist.
Einzige Anpassung: variantenbewusstes Anbieten der Firmware-Updates (6.4).

## 3. Messquellen-Abstraktion

Der einzige echte Code-Unterschied zwischen den Varianten ist die
Gewinnung des Messwerts. `sensor.h` definiert ein gemeinsames Interface,
pro Variante wird genau **eine** `sensor_*.c` mitkompiliert:

```c
/* sensor.h – von jeder Variante genau einmal implementiert */
void     sensor_init(void);            /* Frontend hochfahren            */
uint8_t  sensor_read(sensor_mess *m);  /* 1 = gueltig, 0 = Fehler        */
/* Umrechnung Rohmesswert -> Fuellhoehe 0..10000 (= 0..100,00 %).
 * Druck: steigt mit Fuellstand. Ultraschall: Abstand faellt mit
 * Fuellstand -> Zwei-Punkt-Kalibrierung (leer/voll). */
uint16_t sensor_level(const calib_data *cal);
```

Alles darueber (BLE, NMEA2000, Config, 11-Punkt-Linearisierung, OTA) liegt
in `/core/` und ist fuer alle identisch. Die Tankform-Linearisierung greift
unveraendert: erst Rohwert -> Hoehenanteil (variantenspezifisch), dann die
gemeinsame Tabelle Hoehe -> Volumen.

### Kalibriermodell

- **Druck**: ein Referenzpunkt (aktueller Druck = 100 %), Null = 0 Druck.
- **Ultraschall**: zwei Punkte noetig – Abstand bei leerem und bei vollem
  Tank. `level = (d_leer - d) / (d_leer - d_voll)`. Der Config-Store hat
  Platz fuer den zweiten Kalibrierwert; das Kommandoset bekommt dafuer
  `CALEMPTY` / `CALFULL` (analog zu `CAL100`).

## 4. Build & Varianten

- Je Variante eine **Build-Konfiguration** (CubeIDE) bzw. ein Makefile-Ziel
  (`make VARIANT=ultrasonic`), das die passende `sensor_*.c` + den
  `/variants/<x>/`-Board-Anteil einbindet und `-DHW_VARIANT=<id>` setzt.
- Die GitHub-Action baut auf einen Tag hin per **Matrix alle Varianten**
  auf einmal und haengt je Variante ein Artefakt an das Release.
- Kern und Bootloader werden von allen Konfigurationen aus `/core/` bzw.
  `/bootloader/` gezogen – keine Kopien.

## 5. Versionierung

Der geteilte Kern traegt **eine** Versionsnummer (`version.h`). Ein Fix im
BLE-Teil hebt damit alle Varianten gemeinsam, z. B. auf 1.3.0 – ein Tag,
mehrere Binaries. Varianten unterscheiden sich im Dateinamen/Tag-Suffix,
nicht in der Kernversion. Mentales Modell: „1.3.0 = dieser Kern, in allen
Varianten".

Release-Dateibenennung mit Variante, z. B.:
`LevelSense-ultrasonic-1.3.0_app_meta.hex`.

## 6. Variantenkennung & Schutz vor Cross-Flashing

Drei Varianten heissen drei Firmware-Binaries. Ultraschall-Firmware auf ein
Druck-Board zu flashen ergibt einen kaputten Sensor. Deshalb bekommt jede
Platine eine **hardware-verankerte, erase-feste Variantenkennung**.

### 6.1 Warum OTP

Der OTP-Bereich des STM32G0B1 (One-Time-Programmable, 1 KB ab
`0x1FFF7000` – in RM0444 gegenpruefen) ist ideal, weil er:

- einen **Werksreset und ein komplettes Flash-Erase uebersteht** (im
  Gegensatz zum Config-Store, der beim Werksreset geloescht wird),
- von fehlerhaftem App-Code **nicht versehentlich ueberschrieben** werden
  kann (ein auf 0 gebranntes Bit wird nie wieder 1),
- wie normaler Flash per `HAL_FLASH_Program` (Doppelwort) beschrieben wird.

**Haken – Unwiderruflichkeit:** Ein Fehlbrand ist permanent. Abgefedert
durch das Slot-Schema (6.2) und ein verifizierendes Provisionierungs-
Kommando (6.3).

### 6.2 OTP-Record-Format (Slot-Schema)

Ein Record = ein Doppelwort (8 Byte). Es werden mehrere Slots hintereinander
reserviert (z. B. 8 Slots = 64 Byte); die Firmware nutzt den **letzten
gueltigen** Slot. So bleiben nach einem Fehlgriff noch Korrektur-Schreib-
vorgaenge uebrig, ohne den Chip zu verlieren.

```
Offset  Feld
0..1    MAGIC        (z. B. 0xB0A7)     -> Slot belegt & gueltig
2..3    variant_id   (uint16 LE)        -> 1000=Druck V1, 1001=Druck V2,
                                            1002=Ultraschall
4..5    hw_rev       (uint16 LE)        -> Feindetail/Board-Revision
6..7    crc16        ueber Byte 0..5
```

Lesen: Slots 0..N-1 scannen, hoechsten Slot mit gueltigem MAGIC **und**
CRC nehmen. Leeres OTP (alles 0xFF) = **unprovisioniert** -> Firmware
laeuft in einem sicheren „bitte provisionieren"-Zustand, **kein Bricken**.

### 6.3 Provisionierung (Erst-Inbetriebnahme)

Ein gesondertes Kommando (BLE/UART), das **nur** schreibt, wenn es einen
freien Slot gibt, und den Wert vor dem Commit zurueckliest/verifiziert:

```
PROVHW <variant_id> <hw_rev>
  -> lehnt ab, wenn schon ein gueltiger Slot existiert und passt
  -> schreibt sonst den naechsten freien Slot, liest zurueck, bestaetigt
```

Fuer Serienfertigung besser ein verdrahteter Jig; fuer Kleinserie genuegt
das guarded Kommando. In jedem Fall: nur einmal, nur nach Verifikation.

### 6.4 Zwei Schutzebenen zur Laufzeit

1. **Firmware-Selbstcheck** (Boot): einkompilierte `HW_VARIANT`-Konstante
   gegen OTP `variant_id`. Bei Abweichung -> sicherer Fehlermodus mit
   eigener Blink-/STAT-Meldung, statt z. B. blind einen nicht vorhandenen
   I2C-Drucksensor anzusprechen. Faengt auch manuelles Fehl-Flashen per
   ST-Link ab.
2. **App-seitiges Update-Filtern**: Der Sensor meldet die OTP-Variante im
   STAT (neues Feld, z. B. `HWV=1002`). Die App filtert die
   GitHub-Release-Assets nach dieser Kennung und bietet nur die **passende**
   Firmware an. Das ist der Hauptschutz im Alltag.

Der Selbstcheck sitzt in der App-Firmware, **nicht** im Bootloader – der
bleibt variantenunabhaengig und simpel.

### 6.5 Alternativen (abgewogen)

- **GPIO-/Widerstands-Kodierung**: ein/zwei Pins oder ein ADC-Teiler je
  Bestueckungsvariante, beim Boot ausgelesen. *Reversibel* (kein permanenter
  Fehlbrand), Variante steckt ohnehin in der BOM. Fuer Kleinserie oft
  pragmatischer, kostet aber Pins.
- **Laufzeit-Auto-Erkennung**: Sensortyp (Druck vs. Ultraschall) kann die
  Firmware oft selbst erkennen (I2C-Drucksensor auf 0x6D anpingen; sonst
  UART-Ultraschall). Unterscheidet aber elektrisch aehnliche Druckvarianten
  (V1 vs. V2) meist **nicht** -> hoechstens *zusaetzlich* zum OTP/Strap.

Empfehlung: OTP bei kontrollierter Erst-Inbetriebnahme + Slot-Schema; wer
Fehlbraende scheut, nimmt die Pin-Kodierung (funktional gleichwertig).

## 7. Bootloader-Integration

Den Bootloader ins selbe Mono-Repo als `/bootloader/` ziehen. Dann ist
`dfu_common.c` buchstaeblich **eine** Datei fuer App und Bootloader – das
„muss bit-identisch bleiben"-Problem verschwindet. Der Bootloader ist
variantenunabhaengig (kennt nur Flash-Layout + CRC), also einer fuer alle.
Er wertet die Variante nicht aus (kein OTP-Check im Bootloader).

## 8. Migrationsplan (risikoarm, jeweils mit „baut identisch?"-Check)

1. **Mono-Repo anlegen**, aktuelle Firmware als `core/` +
   `variants/pressure_v1/` einsortieren. Ziel: baut **bit-identisch** wie
   heute (gleiche CRC/gleiches Binary) – erst dann weiter.
2. **Bootloader** als `/bootloader/` hineinziehen, `dfu_common.c` auf die
   eine gemeinsame Datei umstellen. Bootloader baut identisch.
3. **Messquellen-Abstraktion** einziehen: aktuelle Messung in
   `sensor_pressure_v1.c` hinter `sensor.h` kapseln; Kern ruft nur noch das
   Interface. Wieder Identitaets-Check.
4. **Variantenmechanismus** ergaenzen: `hw_variant.c/.h` (OTP lesen +
   Selbstcheck), `HWV`-Feld im STAT, `PROVHW`-Kommando, App-Filter.
5. **Neue Varianten** als weitere `sensor_*.c` + `variants/<x>/` +
   Build-Ziele hinzufuegen (Ultraschall mit Zwei-Punkt-Kalibrierung).
6. CI auf **Matrix-Build** aller Varianten umstellen; Release-Assets je
   Variante benennen.

Erst wenn Schritt 1–3 nachweislich identische Binaries liefern, kommen die
neuen Varianten dazu – so bleibt der Umzug jederzeit ueberpruefbar.
