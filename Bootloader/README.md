# OTA-Bootloader

Bootloader für das Firmware-Update über BLE. Er liegt bei `0x08000000`, prüft
beim Start die App und geht bei Bedarf in den DFU-Empfangsmodus. Gesamtentwurf:
siehe `DESIGN.md`.

## Dateien

- `Core/Src/main.c` – Entscheidung (App/DFU), Flash-Zugriffe, App-Sprung, DFU-Schleife
- `Core/Src/boot_proteus.c`, `Core/Inc/boot_proteus.h` – Proteus-e-UART-Treiber (Polling)
- `Core/Src/boot_dfu.c`, `Core/Inc/boot_dfu.h` – DFU-Ablauf (hardwareunabhängig, getestet)
- `STM32G0B1KBUXN_BOOT.ld` – Linker-Skript (Bootloader bei `0x08000000`, 32 KB)
- `tools/make_meta.py` – erzeugt die Metadaten für die Erst-Inbetriebnahme
- gemeinsam mit der App: `../Core/Inc/dfu_common.h`, `../Core/Src/dfu_common.c`

## Bootloader-Projekt in STM32CubeIDE anlegen

Ein direktes „Import existing project" ist nicht möglich – hier liegen nur die
Quelldateien, kein komplettes CubeIDE-Projekt (Startup, System-Datei, HAL,
`.project`/`.cproject` fehlen). Am einfachsten das **App-Projekt duplizieren**,
damit diese Infrastruktur schon passt:

1. In CubeIDE im Project Explorer das App-Projekt **kopieren und einfügen**
   (Rechtsklick → Copy, dann Paste) und einen neuen Namen vergeben, z. B.
   `CAN_Fuellstandsensor_Boot`.
2. Im Duplikat das Linker-Skript auf **`STM32G0B1KBUXN_BOOT.ld`** setzen
   (Properties → C/C++ Build → Settings → MCU GCC Linker → General → Linker
   Script) – oder in der vorhandenen `.ld` die FLASH-Region auf
   `ORIGIN = 0x08000000, LENGTH = 32K` ändern.
3. Diese Dateien aus `Bootloader/Core/` ins Duplikat übernehmen (die
   gleichnamigen App-Dateien ersetzen bzw. ergänzen):
   `Core/Src/main.c`, `Core/Src/stm32g0xx_it.c`, `Core/Inc/main.h`,
   `boot_dfu.*`, `boot_proteus.*` sowie `dfu_common.*` (aus dem App-`Core/`).
4. Die App-only-Module **aus dem Build nehmen** (Rechtsklick auf die Datei →
   Resource Configurations → Exclude from Build): `nmea200.c`, `ee.c`,
   `config_store.c`, `ble.c`. `stm32g0xx_hal_msp.c` bleibt (liefert die
   UART-/GPIO-MspInit); die nicht genutzten MspInit-Teile (DAC/FDCAN/I2C/TIM)
   stören nicht.
5. Bauen. Der Bootloader nutzt keine Peripherie-Interrupts (UART per Polling),
   deshalb reicht das minimale `stm32g0xx_it.c` aus diesem Ordner.

## Erst-Inbetriebnahme per SWD (einmalig)

Beim ersten Mal müssen Bootloader, App **und** die Metadaten aufs Gerät. Metadaten
aus der gebauten App-`.bin` erzeugen:

```bash
python Bootloader/tools/make_meta.py firmware.bin meta.bin
```

Dann mit STM32CubeProgrammer flashen:

| Datei | Adresse |
|---|---|
| Bootloader `.bin`/`.hex` | `0x08000000` |
| App `firmware.bin` | `0x08008000` |
| `meta.bin` | `0x0801E800` |

Danach startet der Bootloader, erkennt die gültige App und springt hinein.

## Update im Betrieb (OTA)

1. In der App „Firmware-Update" starten → sendet das Kommando `DFU` an den Sensor.
2. Die App setzt das Enter-DFU-Magic und startet neu → landet im Bootloader.
3. Der Bootloader initialisiert das Proteus-Modul, es advertised; die App
   verbindet sich neu und überträgt die neue `firmware.bin` (DFUS/DFUD/DFUE).
4. Nach erfolgreicher CRC-Prüfung schreibt der Bootloader die Metadaten und
   startet neu → die neue App läuft.

Schlägt der Transfer fehl (Abbruch, Stromausfall, CRC), bleibt der Marker
ungültig → das Gerät bleibt beim nächsten Start im Bootloader und wartet auf
einen neuen Versuch. Letzte Rückfallebene ist immer SWD.

## Grenzen

- Ein CRC-korrektes, aber im Betrieb fehlerhaftes Image kann nur durch ein
  erneutes Update ersetzt werden (kein zweiter Slot / Rollback).
- Optionale spätere Härtung: Flash-Schreibschutz (WRP) auf `0x08000000`–`0x08007FFF`,
  Watchdog, signierte Images.
