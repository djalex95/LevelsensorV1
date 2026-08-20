/*
 * ble.h
 *
 * Treiber für das Würth Elektronik Proteus-e (2612011024000) an USART2.
 * Implementiert das Proteus-UART-Kommandoprotokoll (SPP-like Profil) und
 * stellt einen transparenten Datenkanal für eine Handy-App bereit.
 *
 *  Created on: Jun 9, 2024
 *      Author: a_hae
 */

#ifndef INC_BLE_H_
#define INC_BLE_H_

#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_uart.h"

/* Proteus-UART-Frame: 0x02 | CMD | Len(2, LE) | Payload | CS(XOR aller vorherigen)
 * Kommando-Schema: REQ = base, CNF = base|0x40, IND = base|0x80, RSP = base|0xC0.
 * HINWEIS: Werte gegen das Proteus-e User Manual gegenprüfen, falls ein Kommando
 * nicht wie erwartet reagiert. */
#define BLE_STX                 0x02
#define CMD_RESET_REQ           0x00
#define CMD_GETSTATE_REQ        0x01
#define CMD_GETSTATE_CNF        0x41
#define CMD_SLEEP_REQ           0x02
#define CMD_DATA_REQ            0x04
#define CMD_DATA_CNF            0x44
#define CMD_DATA_IND            0x84
#define CMD_CONNECT_IND         0x86
#define CMD_DISCONNECT_IND      0x87
#define CMD_SECURITY_IND        0x88
#define CMD_CHANNELOPEN_RSP     0xC6
#define CMD_TXCOMPLETE_RSP      0xC4
#define CMD_SET_REQ             0x11
#define CMD_SET_CNF             0x51	/* Antwort: Status(1), 0x00 = ok       */
#define CMD_GET_REQ             0x10	/* Setting lesen                       */
#define CMD_GET_CNF             0x50	/* Antwort: Status(1) + Wert           */
#define CMD_DISCONNECT_REQ      0x07
#define CMD_DELETEBONDS_REQ     0x0E	/* Bonding-Daten loeschen (Len 0=alle) */
#define CMD_DELETEBONDS_CNF     0x4E
#define CMD_GETBONDS_REQ        0x0F	/* Bond-Tabelle abfragen               */
#define CMD_GETBONDS_CNF        0x4F	/* Status(1)+Anzahl(1)+je Bond ID(2)+MAC(6) */
#define CFG_IDX_DEVICENAME      0x02	/* Settings-Index RF_DeviceName        */
#define CFG_IDX_SECFLAGS        0x0C	/* Settings-Index RF_SecFlags (1 Byte) */
#define CFG_IDX_STATICPASSKEY   0x12	/* Settings-Index RF_StaticPasskey (6) */

/* Ziel-Sicherheitsmodus: Static Passkey (0x03) + Bonding (Bit 3) = 0x0B.
 *
 * Zweiter Anlauf. Der erste scheiterte NICHT am Modul (Bonds liegen laut
 * Handbuch und Wuerth-Treiber persistent im Modul-Flash, bis zu 12 Stueck),
 * sondern an zwei eigenen Fehlern:
 *  1. Die PIN wurde anfangs bei jedem Boot zurueckgelesen/verglichen und bei
 *     (systematischer) Abweichung neu geschrieben - inkl. Bond-Loeschung.
 *  2. Die Provisionierung feuerte SET/DELETEBONDS/RESET blind mit 50-ms-
 *     Pausen: der Proteus-e startet aber nach JEDEM CMD_SET_REQ selbst neu,
 *     Folgekommandos fielen in den Modul-Boot und gingen verloren (der
 *     Wuerth-Referenztreiber wartet deshalb nach jedem Set auf die
 *     Boot-Meldung CMD_GETSTATE_CNF).
 * Jetzt: Schritt-Kette in der Hauptschleife, jedes Kommando mit bestaetigtem
 * CNF und abgewartetem Modul-Neustart (ble_boot_seen). */
#define BLE_SECFLAGS_TARGET     0x0B

/* Laenge der statischen Passkey (6 Ziffern, ASCII). Der Werkswert steht in
 * app_config.h (SENSOR_PIN_DEFAULT) und entspricht dem Modul-Default. */
#define BLE_PIN_LEN             6

#define BLE_MAX_PAYLOAD         243
#define BLE_FRAME_MAX           (BLE_MAX_PAYLOAD + 5)

/* Verbindungszustand (in der ISR gesetzt, in der Hauptschleife gelesen) */
extern volatile uint8_t ble_connected;      /* 1 zwischen CONNECT_IND und DISCONNECT_IND */
extern volatile uint8_t ble_channel_open;   /* 1 nach CHANNELOPEN_RSP – erst dann senden */

/* Empfangene Nutzdaten (CMD_DATA_IND) für die Hauptschleife */
extern volatile uint8_t ble_data_ready;
extern uint8_t          ble_data_buf[BLE_MAX_PAYLOAD];
extern volatile uint16_t ble_data_len;

/* Modul zurücksetzen und UART-Empfang starten. */
void BLE_Init(UART_HandleTypeDef *huart);

/* Einzelnes empfangenes UART-Byte in den Frame-Parser geben (aus RX-Callback). */
void BLE_ProcessByte(uint8_t b);

/* Nutzdaten über den offenen Kanal an die App senden (CMD_DATA_REQ).
 * Rückgabe 0 = nicht gesendet (Kanal zu / Modul busy / Fehler). */
uint8_t BLE_SendData(const uint8_t *data, uint16_t len);

/* Komfort: nullterminierten Text senden. */
uint8_t BLE_SendString(const char *s);

/* Ändert den BLE-Gerätenamen (RF_DeviceName) dauerhaft im Modul-Flash.
 * Da CMD_SET_REQ nur im getrennten Zustand erlaubt ist, wird bei bestehender
 * Verbindung zuerst getrennt; die Hauptschleife wendet den Namen danach an.
 * Das Modul startet anschließend selbstständig neu. */
uint8_t BLE_SetDeviceName(const char *name);

/* Muss in der Hauptschleife aufgerufen werden, wenn ble_setname_pending gesetzt
 * ist und die Verbindung getrennt wurde (ble_connected == 0). */
void BLE_ApplyPendingName(void);

/* 1 = es liegt eine aufgeschobene Namensänderung vor (nach Trennung anwenden). */
extern volatile uint8_t ble_setname_pending;

/* Fragt eine Moduleinstellung ab (CMD_GET_REQ). Da die Antwort (CMD_GET_CNF)
 * den Settings-Index nicht enthält, wird er hier gemerkt - es darf immer nur
 * EINE Anfrage offen sein. Ergebnis landet asynchron in ble_get_value/-_len;
 * ble_get_ready wird dann 1. Rückgabe 0 = Sendefehler. */
uint8_t BLE_RequestSetting(uint8_t idx);

extern volatile uint8_t  ble_get_ready;
extern uint8_t           ble_get_index;      /* Index der offenen Anfrage    */
extern uint8_t           ble_get_value[21];  /* Wert (nullterminiert)        */
extern volatile uint16_t ble_get_len;

/* --- Bausteine der Sicherheits-Provisionierung (nicht blockierend) ---
 * Jede Funktion schickt genau EIN Kommando; die Bestaetigungen setzt der
 * Parser als Flags. Die Schritt-Kette (Reihenfolge, Timeouts, Wieder-
 * holungen) liegt in der Hauptschleife (main.c, ble_sync). */

/* Vor dem Senden loeschen, danach pollen: 0 = offen, 1 = ok, 2 = Fehler */
extern volatile uint8_t ble_set_cnf;        /* Antwort auf CMD_SET_REQ      */
extern volatile uint8_t ble_delbonds_cnf;   /* Antwort auf CMD_DELETEBONDS  */

/* 1 sobald das Modul (neu) gebootet hat - es meldet sich nach jedem Start
 * spontan mit CMD_GETSTATE_CNF. Vor einem erwarteten Neustart loeschen. */
extern volatile uint8_t ble_boot_seen;

/* Bond-Tabelle: BLE_RequestBonds() senden, dann ble_bonds_ready pollen
 * (1 = ok, 2 = Fehler); Anzahl steht in ble_bonds_count. */
extern volatile uint8_t ble_bonds_ready;
extern volatile uint8_t ble_bonds_count;
extern volatile uint8_t ble_bonds_status;  /* Status-Byte der CNF (Diagnose) */

/* Letzter per CMD_SECURITY_IND gemeldeter Sicherheitszustand der aktuellen
 * Verbindung (0x00 = re-bonded, 0x01 = neu gebondet; 0xFF = noch keiner). */
extern volatile uint8_t ble_sec_state;

uint8_t BLE_SetSecFlags(uint8_t flags);          /* CMD_SET RF_SecFlags      */
uint8_t BLE_SetPasskey(const char *pin6);        /* CMD_SET RF_StaticPasskey */
uint8_t BLE_DeleteBonds(void);                   /* CMD_DELETEBONDS_REQ      */
uint8_t BLE_ResetModule(void);                   /* CMD_RESET_REQ            */
uint8_t BLE_RequestBonds(void);                  /* CMD_GETBONDS_REQ         */

/* Fragt den Modulzustand ab (CMD_GETSTATE_REQ). Die Antwort CMD_GETSTATE_CNF
 * ist dieselbe Meldung, die das Modul nach einem Neustart von sich aus
 * schickt, und setzt ble_boot_seen. Damit haengt die Provisionierungs-Kette
 * nicht daran, dass die spontane Boot-Meldung tatsaechlich ankommt: sobald
 * das Modul wieder antwortet, ist es wieder da. */
uint8_t BLE_RequestState(void);                  /* CMD_GETSTATE_REQ         */

/* --- Diagnose ---------------------------------------------------------
 *
 * Solange die Kopplung scheitert, ist der Datenkanal ueber BLE genau das,
 * was fehlt - Fehlersuche ueber BLE geht also nicht. Deshalb schreibt der
 * Treiber jedes Ereignis in einen kleinen Ringpuffer, der ueber NMEA2000
 * ausgelesen wird (PROP_CMD_BLEDIAG). Damit laesst sich im Nachhinein
 * unterscheiden, ob das Modul neu gestartet ist (CMD_GETSTATE_CNF), ob es
 * die Verbindung selbst beendet hat (CMD_DISCONNECT_IND samt Grund) und ob
 * der STM32 in diesem Moment ueberhaupt etwas gesendet hat. */
#define BLE_LOG_N               24

typedef struct
{
	uint16_t t;		/* Zeit in 1/10 s seit STM32-Start (rollt nach ~109 min) */
	uint8_t  ev;	/* Modul -> STM32: Kommandobyte (immer >= 0x41, denn
					 *                 CNF = base|0x40, IND = base|0x80,
					 *                 RSP = base|0xC0)
					 * STM32 -> Modul: 0x20 | Kommando (Kommandos sind <= 0x11,
					 *                 kollidiert also nicht mit den Antworten -
					 *                 0x80 waere mit CMD_DISCONNECT_IND
					 *                 zusammengefallen, ausgerechnet dem
					 *                 wichtigsten Ereignis)
					 * 0xFE          : STM32 gestartet (BLE_Init) */
	uint8_t  p;		/* erstes Nutzbyte (Status/Grund) bzw. Settings-Index */
} ble_log_entry;

extern volatile ble_log_entry ble_log[BLE_LOG_N];
extern volatile uint8_t ble_log_wr;	/* naechster Schreibindex */
extern volatile uint8_t ble_log_cnt;	/* gefuellte Eintraege (max. BLE_LOG_N) */

#define BLE_LOG_EV_MCUBOOT      0xFE

/* Ein Sendeversuch wurde unterdrueckt, weil die Verbindung noch nicht
 * verschluesselt ist. Nutzbyte = ble_channel_open zum Zeitpunkt des Versuchs. */
#define BLE_LOG_EV_TXBLOCK      0x3F

void BLE_LogAdd(uint8_t ev, uint8_t p);

/* --- Langzeit-Protokoll ------------------------------------------------
 *
 * Der Ringpuffer oben faengt jedes Ereignis ein und ist im Normalbetrieb
 * nach wenigen Minuten ueberschrieben. Ein Fehler, der erst nach Stunden
 * auftritt - etwa eine Kopplung, die sich ueber Nacht verabschiedet -
 * laesst sich damit nicht einfangen. Dafuer gibt es dieses zweite, grobe
 * Protokoll: nur die seltenen, aussagekraeftigen Ereignisse, dafuer mit
 * Zeitstempel in Sekunden (32 Bit, laeuft praktisch nicht ueber). Dazu
 * Zaehler, die unabhaengig vom Puffer weiterlaufen - selbst wenn der
 * Puffer laengst umgelaufen ist, bleibt die Anzahl sichtbar.
 * Ausgelesen wird beides ueber NMEA2000 (PROP_CMD_BLEEVT).
 */
#define BLE_EVT_N 24

typedef struct
{
	uint32_t t;		/* Sekunden seit STM32-Start */
	uint8_t  ev;	/* BLE_EVT_*                 */
	uint8_t  p;		/* Zusatzbyte, Bedeutung je Ereignis */
} ble_evt_entry;

#define BLE_EVT_MCUBOOT   0x01	/* STM32 gestartet                          */
#define BLE_EVT_MODBOOT   0x02	/* Funkmodul gestartet,  p = Status         */
#define BLE_EVT_SEC       0x03	/* Pairing, aber nicht mit bekanntem Bond;
								 * p = Status (0x01 = frisch gekoppelt)    */
#define BLE_EVT_MODERR    0x04	/* ERROR_IND des Moduls, p = Status         */
#define BLE_EVT_DELBONDS  0x05	/* Bonds im Modul geloescht                 */
#define BLE_EVT_MODRESET  0x06	/* Modul-Reset gesendet                     */
#define BLE_EVT_HEAL      0x07	/* Selbstheilung ausgeloest, p = Heilung Nr. */
#define BLE_EVT_DISCNOSEC 0x08	/* Verbindung endete unverschluesselt,
								 * p = ble_fail_cnt danach                  */

/* Zaehler seit dem Start des STM32. */
typedef struct
{
	uint32_t conn;			/* Verbindungen                          */
	uint32_t conn_sec;		/* davon verschluesselt beendet          */
	uint32_t conn_nosec;	/* ohne Verschluesselung beendet         */
	uint32_t heal;			/* Selbstheilungen                       */
	uint32_t heal_t;		/* Laufzeit (s) bei der letzten Heilung  */
	uint32_t modboot;		/* Neustarts des Funkmoduls              */
} ble_stat_t;

extern volatile ble_evt_entry ble_evt[BLE_EVT_N];
extern volatile uint8_t ble_evt_wr;	/* naechster Schreibindex           */
extern volatile uint8_t ble_evt_cnt;	/* gefuellte Eintraege (max. BLE_EVT_N) */
extern ble_stat_t ble_stat;

void BLE_EvtAdd(uint8_t ev, uint8_t p);

/* Zustandsbild fuer die NMEA2000-Diagnose. Wird in main.c gepflegt. */
typedef struct
{
	uint8_t reset_cause;	/* RCC->CSR >> 24 beim Start (Grund des Neustarts) */
	uint8_t sync_step;		/* ble_sync_step   */
	uint8_t secprov_sub;	/* secprov_sub     */
	uint8_t secprov_att;	/* secprov_att     */
	uint8_t marker;			/* cfg_data[CFG_SECPROV_OFF] */
	uint8_t heal_cnt;		/* bond_heal_cnt   */
	uint8_t fail_cnt;		/* ble_fail_cnt    */
	uint8_t rb_flags;		/* 1 = SecFlags, 2 = Passkey, 4 = Modul-FW gelesen */
	uint8_t secflags;		/* zurueckgelesener RF_SecFlags-Wert */
	uint8_t passkey[BLE_PIN_LEN];	/* zurueckgelesener RF_StaticPasskey */
	uint8_t modfw[3];		/* Modul-Firmware: Major, Minor, Patch */
} ble_diag_t;

extern ble_diag_t ble_diag;

/* Aktive Verbindung modulseitig trennen (CMD_DISCONNECT_REQ). */
void BLE_Disconnect(void);

#endif /* INC_BLE_H_ */
