/*
 * config_store.h
 *
 * Robuster Konfigurationsspeicher: Zwei-Pages-Ping-Pong (Flash-Pages 62/63)
 * mit Sequenzzaehler und CRC16. Atomar gegen Stromausfall: die alte Kopie
 * bleibt gueltig, bis die neue vollstaendig und pruefsummenkorrekt im Flash
 * steht.
 *
 * Layout des 64-Byte-Config-Blocks (cfg_data, Layout-Version 2):
 *   0      Kalibrierung vorhanden (0xFF = nein)
 *   1..4   max_val (uint32 LE)
 *   5..6   Nullpunkt-Offset (int16 LE, uBar; gueltig nur mit Marker in 7)
 *   7      Marker CFG_FIELD_MAGIC: Offset in 5..6 wurde beschrieben
 *   8      DAC-Kalibrierung vorhanden
 *   9..16  dac_mx, dac_c (je int32 LE)
 *   17     fluidType
 *   18     Kapazitaet (Liter)
 *   19..29 11 Linearisierungs-Stuetzstellen
 *   30     NMEA2000-Quelladresse
 *   31     Geraete-/Tank-Instanz
 *   32     Layout-Version (2)
 *   33..56 Sensorname (24 Byte, 0x00-terminiert/-gepolstert; 0xFF = nie gesetzt)
 *   57..58 EMA-Filter 'wertung' (uint16 LE, Promille Altanteil 0..990;
 *          gueltig nur mit Marker in 59)
 *   59     Marker CFG_FIELD_MAGIC: wertung in 57..58 wurde beschrieben
 *   60..62 BLE-PIN als Zahl (uint24 LE, 0..999999; 0xFFFFFF = Werks-PIN
 *          123123). Gepackt statt ASCII, weil nur 3 Bytes frei sind.
 *   63     Security-Provisioning-Marker (CFG_SECPROV_MAGIC = BLE-Sicherheit
 *          wurde nach Werksreset/Erstboot einmalig provisioniert; sonst 0xFF)
 *
 * Bytes 0..31 sind identisch zum alten 32-Byte-Format (Layout-Version 1).
 * Beim ersten Boot nach einem Update migriert config_load() einen
 * vorhandenen v1-Datensatz automatisch - Kalibrierung, Tankform, Adresse
 * usw. bleiben erhalten. Der alte Datensatz bleibt bis zum naechsten Save
 * als Backup in seiner Page liegen. (Achtung: Einbahnstrasse - eine aeltere
 * Firmware kann das v2-Format nicht lesen.)
 */

#ifndef INC_CONFIG_STORE_H_
#define INC_CONFIG_STORE_H_

#include <stdint.h>

#define CFG_SIZE      64
#define CFG_VER_OFF   32          /* Layout-Versionsbyte                    */
#define CFG_LAYOUT_V  2
#define CFG_NAME_OFF  33          /* Sensorname (Installation Description)  */
#define CFG_NAME_LEN  24
#define CFG_OFS_OFF   5           /* Nullpunkt-Offset int16 LE, Marker @7   */
#define CFG_FILT_OFF  57          /* EMA-Filter uint16 LE, Marker @59       */
#define CFG_PIN_OFF   60          /* BLE-PIN uint24 LE, 0xFFFFFF = Werk     */
/* Marker "Feld wurde beschrieben". Bewusst weder 0x00 noch 0xFF und keine
 * ASCII-Ziffer - so wird eine frueher an 57..62 gespeicherte BLE-PIN oder
 * ein geloeschter/genullter Altbestand nie als gueltiger Wert gelesen. */
#define CFG_FIELD_MAGIC 0xA7
/* 60..62 frei                                                             */
#define CFG_SECPROV_OFF   63      /* Marker: BLE-Sicherheit einmalig provisioniert */
/* Wert je Sicherheits-Generation geaendert (0xA5 -> 0x5A -> 0xB5): steht ein
 * ANDERER Wert im Config, provisioniert der Boot-Abgleich einmalig neu.
 * 0xB5 = Static Passkey + Bonding aktiv (zweiter PIN-Anlauf); Sensoren mit
 * altem Marker stellen sich beim Update automatisch um. */
#define CFG_SECPROV_MAGIC 0xB5

/* RAM-Cache der Konfiguration; Aenderungen hier eintragen, dann config_save() */
extern uint8_t cfg_data[CFG_SIZE];

/* Beim Boot aufrufen: laedt den neuesten gueltigen Datensatz. Erkennt und
 * migriert automatisch das v1-Format (32 Byte) sowie das Ur-Altformat aus
 * Page 63. Rueckgabe 0 = nichts gefunden, cfg_data = 0xFF. */
uint8_t config_load(void);

/* Schreibt cfg_data als neuen Datensatz in die jeweils andere Page.
 * Rueckgabe 0 = Fehler (alter Datensatz im Flash bleibt gueltig). */
uint8_t config_save(void);

/* Aus dem NMI-Handler bei Flash-ECC-Doppelfehler aufrufen: liegt die
 * Fehleradresse im Config-Bereich, wird die betroffene Page geloescht und
 * ein Reset ausgeloest (die andere Page haelt die letzte gueltige Config).
 * Kehrt nur zurueck, wenn die Adresse ausserhalb des Config-Bereichs liegt. */
void config_nmi_recover(uint32_t fail_addr);

/* Werksreset: loescht beide Config-Pages. Nach dem naechsten Boot gelten
 * Werkswerte (Adresse 0x21, unkalibriert, keine Tankform, kein Name,
 * Werks-PIN 123123 - der Boot-Abgleich stellt sie im BLE-Modul wieder her).
 * Rueckgabe 0 = Loeschfehler. */
uint8_t config_factory_reset(void);

#endif /* INC_CONFIG_STORE_H_ */
