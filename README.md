# CAN Füllstandsensor BLE (STM32G0B1)

Füllstandsensor für NMEA2000-Netzwerke (Marine) auf Basis eines STM32G0B1KB.
Misst den Druck per I2C-Sensor, gibt den Füllstand über NMEA2000 (FDCAN),
einen Analogausgang (DAC) und eine RGB-LED aus. Konfiguration und Kalibrierung
sind per Taster oder über ein PC-Tool (PEAK PCAN-USB) möglich.

## Funktionen

- **NMEA2000**: Fluid Level (PGN 127505), Temperatur (PGN 130312), Product/Device
  Info, ISO Address Claim mit NAME-Arbitrierung und persistenter Adresse
- **Konfiguration nach Norm** über Group Function (PGN 126208): Fluidtyp,
  Kapazität, Instanz – mit Acknowledge
- **Tank-Linearisierung**: 11 Stützstellen für unregelmäßige Tankformen
  (proprietäres PGN 126720)
- **100%-Kalibrierung** per Taster oder aus dem PC-Tool
- **Robuster Konfigurationsspeicher**: Zwei-Pages-Ping-Pong im Flash mit CRC,
  stromausfallsicher, inklusive ECC-Recovery
- **BLE (Würth Proteus-e an USART2)**: transparenter Datenkanal für eine
  Handy-App – Live-Anzeige, Konfiguration, Kalibrierung, Tankform, Modulname

## Struktur

Dieses Repository enthält alle drei Teile des Projekts:

- `Core/`, `Drivers/` – **Firmware** (STM32CubeIDE-Projekt).
  Kernquellen: main.c, nmea200.c (NMEA2000), ble.c (Proteus-e),
  config_store.c (Flash-Speicher).
- `PC_Tools/` – **PC-Programm** (Python, PEAK PCAN-USB)
  - `nmea2000_gui.py` – grafische Oberfläche (Live-Anzeige, Kalibrierung, Tankform)
  - `nmea2000_reader.py` – Dekodier-Bibliothek / Konsolen-Mitlese-Tool
  - `BLE_Protokoll.md` – Spezifikation des BLE-Textprotokolls
- `App_Fuellstand/` – **Handy-App** (Flutter) mit denselben Funktionen über BLE
- `Inbetriebnahme.md` – Kurzanleitung zur Inbetriebnahme

## Firmware bauen

Projekt in STM32CubeIDE öffnen und die Debug-Konfiguration bauen/flashen.
Ziel-MCU: STM32G0B1KBU6N.

## PC-Tool starten

```
pip install python-can        # einmalig, PEAK-Treiber muss installiert sein
cd PC_Tools
python nmea2000_gui.py
```
