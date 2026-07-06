# OTA-Firmware-Update über BLE – Entwurf

Ziel: Die STM32-Anwendungsfirmware über die Handy-App (BLE → Proteus-e → UART →
STM32) aktualisieren. Ein Bootloader nimmt das Update entgegen; schlägt es fehl,
bleibt das Gerät im Bootloader und das Update kann erneut gestartet werden.

## 1. Flash-Aufteilung (STM32G0B1KB, 128 KB, Seitengröße 2 KB)

| Bereich | Adresse | Größe | Seiten | Inhalt |
|---|---|---|---|---|
| Bootloader | `0x08000000`–`0x08007FFF` | 32 KB | 0–15 | wird nur per SWD geflasht, nie durch ein Update |
| Anwendung | `0x08008000`–`0x0801E7FF` | 90 KB | 16–60 | die eigentliche Sensor-Firmware (aktuell ~64 KB) |
| DFU-Metadaten | `0x0801E800`–`0x0801EFFF` | 2 KB | 61 | Gültigkeits-Marker + Größe + CRC der App |
| Config-Speicher | `0x0801F000`–`0x0801FFFF` | 4 KB | 62–63 | bestehender Ping-Pong-Konfigspeicher (unverändert) |

RAM: 144 KB. Die **obersten 32 Byte** sind reserviert (`_estack` entsprechend
abgesenkt). Darin liegt das „Enter-DFU"-Magic-Wort, das die laufende App setzt,
bevor sie neu startet – es übersteht einen Warmstart (RAM bleibt erhalten).

## 2. Ablauf beim Start (Bootloader)

1. Prüfen, ob im RAM das **Enter-DFU-Magic** steht (App hat Update angefordert).
2. Prüfen, ob eine **gültige App** vorliegt: Metadaten-Marker gesetzt **und**
   CRC32 über die gespeicherte App-Größe stimmt.
3. Entscheidung:
   - Update angefordert **oder** keine gültige App → **DFU-Modus** (empfangen).
   - Sonst → **VTOR auf `0x08008000` setzen und in die App springen**.

Weil der Gültigkeits-Marker **erst am Schluss** eines erfolgreichen Updates
gesetzt wird, führt jeder Abbruch (Stromausfall, Verbindungsverlust, CRC-Fehler)
dazu, dass beim nächsten Start keine gültige App erkannt wird → das Gerät bleibt
im DFU-Modus und wartet auf einen neuen Versuch. Der Bootloader selbst wird dabei
nie überschrieben.

## 3. DFU-Modus (Bootloader ↔ Handy)

Der Bootloader initialisiert das Proteus-Modul selbst (eigener, abgespeckter
UART-Treiber), das Modul advertised, das Handy verbindet sich erneut. Danach
läuft ein binäres Transferprotokoll über den Datenkanal (jedes Paket mit dem
Proteus-Header `0x01`):

| Nachricht | Richtung | Inhalt | Antwort |
|---|---|---|---|
| `DFUS` Start | App→BL | `"DFUS"` + Größe(4) + CRC32(4) | `DFUS OK` / `DFUS ERR` |
| `DFUD` Daten | App→BL | `"DFUD"` + Offset(4) + Bytes | `DFUD <offset> OK` |
| `DFUE` Ende | App→BL | `"DFUE"` | `DFUE OK` (CRC ok, Reset) / `DFUE ERR` |

Bei `DFUS` löscht der Bootloader den App-Bereich und merkt sich Größe+CRC. Die
Daten werden blockweise geschrieben (Blockgröße ≤ Proteus-MPS). Bei `DFUE` prüft
er die CRC32 über den geschriebenen Bereich; stimmt sie, schreibt er die
Metadaten (Marker+Größe+CRC) und startet neu → App läuft.

## 4. App-seitige Änderungen (bereits umgesetzt)

- **Vektortabelle**: Die App setzt `SCB->VTOR = 0x08008000` beim Start.
- **Linker**: App-Startadresse `0x08008000`, RAM-Reserve für das Magic-Wort.
- **BLE-Kommando `DFU`**: schreibt das Enter-DFU-Magic ins RAM und startet neu
  (`OK DFU`, dann Reset → Bootloader).

## 5. Grenzen / Sicherheit

- Ein Transfer-Abbruch brickt das Gerät nicht (Bootloader überlebt, App wird als
  ungültig erkannt).
- Eine zwar CRC-korrekte, aber fehlerhafte App (Absturz im Betrieb) kann nur
  durch ein erneutes Update ersetzt werden – es gibt keinen zweiten Slot zum
  Zurückrollen. Letzte Rückfallebene ist immer der SWD-Anschluss.
- Optional später: Flash-Schreibschutz (WRP) auf den Bootloader-Bereich,
  Watchdog, signierte Images.

## 6. Reihenfolge der Umsetzung

1. **Fundament (dieser Stand):** Flash-Layout, Linker-Skripte, gemeinsame
   DFU-Definitionen (`dfu_common.h/.c`, CRC32), App-Haken (VTOR + `DFU`-Kommando).
2. **Bootloader-Projekt:** eigenständiges STM32CubeIDE-Projekt bei `0x08000000`
   mit Proteus-Treiber, DFU-Empfang, Flash-Schreiben, Verifikation, App-Sprung.
3. **App (Flutter):** DFU-Ablauf (Datei wählen, `DFU` senden, neu verbinden,
   Transfer mit Fortschritt).
