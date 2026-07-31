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

Dieses Repository enthält die Firmware. Handy-App, OTA-Bootloader,
Provisionierungs-Tool und PC-Programm liegen in eigenen Repositories
(`Fuellstandsensor-App`, `BootloaderCube`, `LevelSense-Provision`,
`LevelSense-NMEA2000`).

- `Core/`, `Drivers/` – **Firmware** (STM32CubeIDE-Projekt).
  Kernquellen: main.c, nmea2000.c (NMEA2000), ble.c (Proteus-e),
  config_store.c (Flash-Speicher).
- `BLE_Protokoll.md` – Spezifikation des BLE-Textprotokolls für die Handy-App
- `Inbetriebnahme.md` – Kurzanleitung zur Inbetriebnahme

## Firmware bauen

Projekt in STM32CubeIDE öffnen und die Debug-Konfiguration bauen/flashen.
Ziel-MCU: STM32G0B1KBU6N.

## PC-Tool

Das PC-Programm für PEAK PCAN-USB – Live-Anzeige, Konfiguration, Kalibrierung,
Tankform – liegt im Repository `LevelSense-NMEA2000`. Dort gibt es unter
Releases eine fertige Windows-.exe; der PEAK-Treiber muss installiert sein.

Ändert sich das proprietäre Protokoll auf PGN 126720 (`Core/Src/nmea_app.c`)
oder die Werksadresse (`Core/Src/app_config.c`), gehört die Änderung dort im
selben Zug nachgezogen.

## Versionsnummern

Die Nummer hat drei Stellen, X.Y.Z, und jede Stelle hat eine feste Bedeutung:
**X** steigt bei einer größeren Änderung, **Y** wenn ein kleineres Feature
dazukommt (etwa eine neue Hardware-Variante), **Z** bei Bugfixes. Das gilt
gleich in allen Repositories; jedes zählt dabei seine eigene Nummer.

Die Firmware zählt in `FW_VERSION` (`Core/Inc/version.h`). Freigegeben wird über
einen Tag `vX.Y.Z`; die CI bricht ab, wenn Tag und `FW_VERSION` nicht
zusammenpassen.
