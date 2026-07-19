# Build: App-Hex mit angehängter Meta automatisch erzeugen

Der Bootloader startet die Anwendung nur, wenn die Metadaten auf Seite 61
(`0x0801E800`: Magic + Größe + CRC32) **exakt** zur geflashten App passen. Da
sich die CRC bei jeder Code-Änderung ändert, muss die Meta bei jedem Flashen neu
erzeugt werden. Damit das nicht mehr von Hand passieren muss, erzeugt das Skript
`make_app_meta_hex.py` aus dem App-Binary automatisch:

- `<projekt>_app_meta.hex` – App (`0x08008000`) **und** passende Meta
  (`0x0801E800`) in einer Datei, in einem Rutsch flashbar (STM32CubeProgrammer)
- `meta.hex` – nur die Meta (praktisch als Zusatz-Image im Debug-Launch)

## 1. Post-Build-Schritt in STM32CubeIDE einrichten

Project → Properties → **C/C++ Build → Settings → Build Steps** → *Post-build
steps* → *Command*:

```
arm-none-eabi-objcopy -O binary "${BuildArtifactFileName}" "${BuildArtifactFileBaseName}.bin" && python "${ProjDirPath}/tools/make_app_meta_hex.py" "${BuildArtifactFileBaseName}.bin" "${BuildArtifactFileBaseName}_app_meta.hex"
```

Nach jedem Build liegen dann im Build-Ordner (`Debug/` bzw. `Release/`):
`<projekt>.bin`, `<projekt>_app_meta.hex` und `meta.hex`.

Hinweise:
- `python` muss auf dem PATH sein. Klappt der Aufruf nicht, `python` durch `py`
  (Windows Python-Launcher) oder den vollen Pfad zur `python.exe` ersetzen.
- `&&` funktioniert unter Windows (cmd) und Linux/macOS (sh). Falls die IDE die
  Zeile nicht akzeptiert, die beiden Befehle in zwei Zeilen aufteilen.

## 2. WICHTIG: „Run/Debug" flasht nur die App, nicht die Meta

Beim normalen **Run/Debug** lädt CubeIDE die `.elf` über den Debugger – das
schreibt **nur die App-Sektionen**, nicht die Meta-Seite. Die frisch erzeugte
`.hex` wird dabei **nicht** benutzt. Zwei Wege, damit der Bootloader die App
trotzdem startet:

**A) Flashen mit STM32CubeProgrammer (empfohlen fürs reine Flashen):**
Die `<projekt>_app_meta.hex` öffnen und flashen – App + Meta sind drin, die
Adressen stecken in der Datei.

**B) Meta beim Debuggen mitladen (bequem für die Entwicklung):**
Run → Debug Configurations → dein Launch → Tab **Startup** → *Add…* und die
`meta.hex` als zusätzliches „Load image and symbols" (nur *Load image*, keine
Symbole) eintragen. Dann schreibt Run/Debug App **und** Meta. Da der Post-Build
die `meta.hex` bei jedem Build passend neu erzeugt, bleibt sie automatisch aktuell.

## Erst-Inbetriebnahme (einmalig)

Ganz frische Chips brauchen zusätzlich den **Bootloader** bei `0x08000000`
(siehe `BootloaderCube`). Danach genügt für App-Updates die
`<projekt>_app_meta.hex` bzw. das OTA-Update über die App (dort schreibt der
Bootloader die Meta selbst).
