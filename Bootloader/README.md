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

Am einfachsten das App-Projekt als Vorlage kopieren, damit HAL, Startup, System-
Datei und das MSP (USART2-/GPIO-Init) schon passen:

1. Neues Projekt für **STM32G0B1KBUxN** anlegen (oder das App-Projekt kopieren).
2. Als Linker-Skript **`STM32G0B1KBUXN_BOOT.ld`** eintragen (Projekt-Properties →
   C/C++ Build → Settings → MCU GCC Linker → General → Linker Script).
3. In `Core/Src`/`Core/Inc` diese Dateien verwenden: `main.c`, `boot_proteus.*`,
   `boot_dfu.*` sowie `dfu_common.*` (aus dem App-`Core/`).
4. App-spezifische Module **aus dem Build nehmen** (nmea200, ee, config_store,
   ble, hal_msp-Anteile für DAC/FDCAN/I2C/TIM werden nicht gebraucht) – wichtig,
   damit der Bootloader in 32 KB passt. `stm32g0xx_hal_msp.c` (nur UART/GPIO) und
   `stm32g0xx_it.c` können bleiben; die UART wird per Polling gelesen, der
   USART2-Interrupt stört nicht.
5. Die `main.h` des Bootloaders braucht nur die BLE-Pin-Defines
   (`BLE_RESET_Pin` PA5, `BLE_MODE_Pin` PB9, `BLE_BUSY_Pin` PB1) und
   `#include "stm32g0xx_hal.h"`.

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
