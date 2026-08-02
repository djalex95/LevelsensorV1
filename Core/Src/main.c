/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  *
  * Druckbasierter Fuellstandsensor mit NMEA2000 (CAN) und Bluetooth LE.
  *
  * NAVIGATION (die Datei ist gross; ein spaeterer Refactor teilt diese Bloecke
  * in eigene Module auf):
  *   - main() + Hauptschleife .............. Messtakt, NMEA-Sendetimer,
  *                                            BLE-Bearbeitung, Boot-Abgleich, LED
  *   - Config-/EEPROM-Helfer ............... ausgelagert nach app_config.c
  *   - Sensor/Mess-Ebene ................... ausgelagert nach sensor_common.c
  *   - LED ................................. set_led, calc_color, blink_LED
  *   - NMEA2000-Handler .................... ausgelagert nach nmea_app.c
  *   - BLE-Kommando-Ebene .................. ausgelagert nach ble_app.c
  *   - CubeMX-generiert .................... SystemClock/MX_*_Init, HAL-Callbacks
  *
  * Der eigentliche Proteus-e-BLE-Treiber liegt in ble.c, der robuste
  * Config-Speicher in config_store.c, der NMEA2000-Stack in nmea2000.c.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_types.h"
#include "app_config.h"
#include "sensor.h"
#include "nmea_app.h"
#include "ble_app.h"
#include "config_store.h"
#include "dfu_common.h"
#include "version.h"
#include "hw_otp.h"
#include "board_pins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* calib_data, dac_calib_data, prod_param, sensor_mess: siehe app_types.h */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* std_press, std_offset: siehe app_types.h */

#define PWM_RED TIM3->CCR1
#define PWM_GREEN TIM3->CCR2
#define PWM_BLUE TIM3->CCR3

/* ERROR_TX_CAN, ERROR_I2C: siehe app_types.h */

#define TEMP_SOURCE_NMEA 2	//NMEA2000 Temperature Source: 2 = Inside Temperature

/* PROP-Defines (PGN 126720): siehe nmea_app.c */


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DAC_HandleTypeDef hdac1;

FDCAN_HandleTypeDef hfdcan1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DAC1_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */
/* Config-/EEPROM-Helfer: siehe app_config.h
 * Sensor-/Mess-Ebene (get_value, calc_percent, ...): siehe sensor.h
 * NMEA2000-Handler (handle_*): siehe nmea_app.h
 * BLE-Kommando-Ebene (ble_send_status, ...): siehe ble_app.h */
void set_led(int32_t red, int32_t green, int32_t blue, int32_t brightness);
void calc_color(int32_t *c_red, int32_t *c_green, int32_t *c_blue, uint16_t percent);
void blink_LED();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* volatile: wird in Interrupt-Callbacks UND Hauptschleife verwendet */
volatile uint8_t error_mode = 0;

/* Hardware-Kennung, z. B. "1003A": aus dem OTP (gueltiger Slot) oder als
 * Fallback aus dem Build (HWV_FULL_STR). Siehe hw_otp.h und Issue #2. */
char hw_id_str[8] = HWV_FULL_STR;
volatile uint8_t hw_otp_mismatch = 0;

uint8_t led_jump = 0;

volatile uint8_t run_mode = 1;
 volatile uint8_t setup_mode = 0;
volatile int32_t raw_press = 0;	/* wird auch im EXTI-Callback gelesen;
									   32 bit -> atomarer Zugriff auf dem M0+ (kein Torn Read) */
volatile int32_t press_unfilt = 0;	/* letzter ungefilterter Messdruck (uBar, offset-
									   korrigiert) fuer die Rohwert-Anzeige der App */

 //EEprom struct
 calib_data EEPROM_values;
 dac_calib_data DAC_EEPROM_values;
 prod_param device_param;

uint32_t time_el = 0, last_run = 0, last_run_nmea=0;
 uint32_t tx_time = 100, nmea_time = 2500;	/* PGN 127505: Norm-Intervall 2,5 s */
 uint32_t last_run_temp = 0, temp_time = 2000;	/* PGN 130312: Norm-Intervall 2 s */
 uint32_t last_run_hb = 0, hb_time = 60000;	/* PGN 126993 Heartbeat: 60 s */

 volatile uint8_t adr_claim = 0;
 volatile uint8_t adr_lost = 0;		/* Adress-Arbitrierung verloren (ISR -> Hauptschleife) */
 volatile uint8_t prod_info = 0;
 volatile uint8_t dev_info = 0;
 uint8_t boot_cfginfo_pending = 1;	/* nach dem Boot einmal 126998 senden (Namensstand
									   auf den Bus, z.B. nach einem Werksreset) */

 uint32_t claim_time = 0;			/* Zeitpunkt des letzten Address Claims (250-ms-Sendepause) */

 uint16_t wertung = std_wertung;	/* EMA-Filter: Anteil ALTER Wert in Promille (0..990).
									   Wird beim Boot aus dem Config geladen, per BLE
									   "FILT" einstellbar (uint16: auch 900 passt rein). */

 //LED-Zeitvariablen
 uint32_t last_run_led = 0;
 volatile uint32_t led_time = 20;
 volatile uint8_t led_up = 1;

 //Zeitvariable für beenden des Setupmodes
 volatile uint32_t sm_started = 0;


 //Virtuelle LED-Variablen, um auch negative Zahlen darzustellen
 int32_t LED_r = 0, LED_g = 0, LED_b = 0;
 volatile int32_t LED_brightness = 0;

 uint16_t percent_val = 0;	//100,00 Prozent = 10000

 volatile int16_t level_led = 0;

sensor_mess sensor_data_rx;

NMEA_parameter_Product p_info;

/* Sensorname (Installation Description 1 in PGN 126998); aus dem Config
 * geladen, per BLE "NAME ..." oder Group Function vom Plotter aenderbar. */
char sensor_name[CFG_NAME_LEN + 1] = "";

NMEA_parameter_Device dev_info_par;



/* BLE (Proteus-e): Status-Streaming-Zeitpunkt */
uint32_t last_run_ble = 0, ble_time = 1000;

/* Einmaliger Abgleich Modul <-> Config nach dem Boot in zwei Schritten:
 * 0 = Name, 1 = einmalige Security-Provisionierung, danach 2 = fertig.
 * Es wird jeweils erst gelesen und nur bei ABWEICHUNG geschrieben
 * (Modul-Flash ~10k Zyklen, jedes Schreiben loest einen Modul-Neustart aus). */
static uint8_t  ble_sync_step = 0;
static uint8_t  ble_sync_wait = 0;		/* 1 = Antwort auf GET ausstehend    */
static uint32_t ble_sync_next = 1500;	/* Modul bootet ~1,5 s               */
static uint8_t  ble_sync_tries = 0;

/* Provisionierungs-Kette (Schritt 1 des Boot-Abgleichs): der Proteus-e
 * startet nach jedem CMD_SET_REQ selbst neu, deshalb wird jedes Kommando
 * einzeln bestaetigt und der Modul-Neustart abgewartet (ble_boot_seen). */
static uint8_t  secprov_sub = 0;	/* 0 Reset, 1 SecFlags, 2 Passkey,
									   3 DeleteBonds, 4 Reset             */
static uint8_t  secprov_wait = 0;	/* 1 = auf CNF/Neustart des Schritts warten */
static uint8_t  secprov_att = 0;	/* Sendeversuche des aktuellen Schritts */
static uint8_t  secprov_ints = 0;	/* Verbindungs-Unterbrechungen dieser Kette */
static uint8_t  secprov_rounds = 0;	/* abgebrochene Ketten-Anlaeufe dieses Boots */

/* Von ble_app.c gesetzt: BONDS-Diagnose angefragt (Antwort asynchron). */
volatile uint8_t bonds_req = 0;		/* 1 = anfragen, 2 = Antwort offen, 3 = Modul-FW abfragen, 4 = FW-Antwort offen */
static uint32_t  bonds_t0 = 0;

/* Bond-Selbstheilung (Sensor-Seite): Haelt das Modul einen alten Bond fuer
 * ein Handy, das seinen eigenen verloren hat, bricht das Pairing dort ab,
 * BEVOR die PIN abgefragt wird - die Verbindung stirbt per Timeout. Muster:
 * Verbindungen kommen, aber weder das Pairing laeuft noch geht der
 * Datenkanal auf. Nach 3 solchen Fehlversuchen in Folge werden die
 * Modul-Bonds geloescht und das Modul neu gestartet; danach klappt das
 * frische Koppeln wieder. Kam dagegen eine Sicherheitsmeldung oder ging
 * der Kanal auf, ist die Kopplung in Ordnung - dann wird NICHTS geloescht. */
static uint8_t ble_prev_conn = 0;	/* Verbindungszustand des letzten Durchlaufs */
static uint8_t ble_chan_seen = 0;	/* Kanal war in dieser Verbindung offen */
static uint8_t ble_sec_seen = 0;	/* Pairing/Verschluesselung lief in dieser
									   Verbindung (CMD_SECURITY_IND kam) */
static uint8_t ble_fail_cnt = 0;	/* Verbindungen ohne offenen Kanal in Folge */
static uint8_t bond_heal_step = 0;	/* 0 = aus, 1 = DeleteBonds gesendet */
static uint8_t bond_heal_cnt = 0;	/* Heilungen dieses Boots */
#define BOND_HEAL_MAX 3				/* mehr als das ist keine Bond-Frage mehr */
static uint32_t bond_heal_t0 = 0;

/* Einmaliges Ruecklesen der Modul-Einstellungen fuer die Diagnose:
 * 0/1 = RF_SecFlags, 2/3 = RF_StaticPasskey, 4/5 = Modul-Firmware,
 * 6 = fertig (gerade Schritte fragen, ungerade warten auf die Antwort). */
static uint8_t  ble_rb_step = 0;
static uint32_t ble_rb_next = 0;
static uint32_t ble_rb_t0 = 0;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	uint8_t error_cnt = 0;

	/* OTA: Die App liegt hinter dem Bootloader (0x08008000). Vektortabelle
	 * dorthin umbiegen, bevor Interrupts aktiv werden. */
	SCB->VTOR = DFU_APP_ADDR;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* Grund des letzten Neustarts sichern, bevor ihn irgendetwas loeschen
   * kann. RCC->CSR Bit 31..24 haelt fest, wer zugeschlagen hat: Option-Byte-
   * Ladung, Reset-Pin, Spannungseinbruch (BOR/PWR), Software-Reset,
   * IWDG, WWDG, Low-Power. Der Wert geht in die NMEA2000-Diagnose - nur so
   * ist von aussen zu erkennen, ob der STM32 sich waehrend eines
   * Kopplungsversuchs selbst neu gestartet hat und warum. */
  ble_diag.reset_cause = (uint8_t)(RCC->CSR >> 24);
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DAC1_Init();
  MX_FDCAN1_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

  __HAL_TIM_CLEAR_FLAG(&htim6,TIM_SR_UIF);

  /* Hardware-Kennung aus dem OTP lesen (Issue #2). Ein gueltiger Slot
   * ueberstimmt die Build-Werte. Widerspruch bei der Variante: Geraet
   * bootet und bleibt per BLE erreichbar (OTA-Rettung), sendet aber
   * keine Sensordaten auf den NMEA-Bus und zeigt ERROR_HWV (rote
   * Fehler-LED, E=-Feld im STAT). OTP leer (Entwicklungsboard):
   * Verhalten wie bisher. */
  {
    const hw_otp_slot_t *hw_slot = hw_otp_find();
    if (hw_slot != NULL)
    {
      snprintf(hw_id_str, sizeof(hw_id_str), "%u%c",
               (unsigned)hw_slot->variant, (char)hw_slot->rev);
      if (hw_slot->variant != HW_VARIANT)
      {
        hw_otp_mismatch = 1;
        error_mode |= ERROR_HWV;
      }
    }
  }





    /* Configure global filter:
       Filter all remote frames with STD and EXT ID
       Reject non matching frames with STD ID and EXT ID */
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
    {
      Error_Handler();
    }

    /* Start the FDCAN module */
	 if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
	 {
	   Error_Handler();
	 }


	 /* Bus-Off-Interrupt aktivieren -> Recovery in HAL_FDCAN_ErrorStatusCallback */
	 if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0) != HAL_OK)
	 {
		 Error_Handler();
	 }


	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

	PWM_RED = 0;
	PWM_GREEN = 0;
	PWM_BLUE = 0;

	HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0);



  //Konfiguration aus dem Flash laden (Zwei-Pages-Ping-Pong mit CRC,
  //inkl. einmaliger Migration des Altformats aus Page 63)
  config_load();

  uint8_t EEPROM_result = check_dac_EEPROM();

  if (EEPROM_result == 0x00)
    {
  	  get_dac_EEPROM(&DAC_EEPROM_values);
    }
  else if( EEPROM_result == 0xFF )
  {
	  DAC_EEPROM_values.dac_c = 0;
	  DAC_EEPROM_values.dac_mx = 6205;	//alt:12409
  }
    else{
  	  /* Unbekannter Wert (z.B. Flash-Korruption): wie "keine Kalibrierung"
  	   * behandeln und mit Defaults weiterbooten. Vorher: Endlosschleife mit
  	   * DAC-Debugausgabe -> Geraet startete nie (CAN/BLE tot). */
  	  DAC_EEPROM_values.calib_available = 0xFF;
  	  DAC_EEPROM_values.dac_c = 0;
  	  DAC_EEPROM_values.dac_mx = 6205;	//alt:12409
    }

  get_EEPROM(&EEPROM_values);	/* liest max_val und - Marker-gesichert - den Nullpunkt-Offset */
  if (!check_EEPROM())
  {
	  /* Werkszustand: explizit als "unkalibriert" markieren. Ohne das blieb
	   * calib_available auf 0x00 (Null-Initialisierung der globalen Variable)
	   * - ausgerechnet der Marker fuer "kalibriert" -> App zeigte CAL=1.
	   * Der Offset wird NICHT verworfen: CAL0 (Nullpunkt) ist von der
	   * 100%-Kalibrierung unabhaengig und wurde ggf. schon gesetzt. */
	  EEPROM_values.calib_available = 0xFF;
	  EEPROM_values.max_val = std_press;
  }

  get_param_eeprom(&dev_info_par, &device_param);
  get_name_eeprom(sensor_name);
  wertung = get_filt_eeprom();


  init_Sensor();
  BLE_Init(&huart2);		/* Proteus-e zurücksetzen und Empfang starten */


  dev_info_par.srcAdr = get_adr_eeprom();	/* zuletzt geclaimte Adresse, Fallback 0x21 */

  /* Unique Number aus der 96-bit-Chip-UID ableiten -> jede Platine ist ohne
   * manuelles Zutun eindeutig (21-bit-Feld im NMEA2000-NAME; 0 wird vermieden). */
  {
    uint32_t uid = *(volatile uint32_t *)(UID_BASE + 0U)
                 ^ *(volatile uint32_t *)(UID_BASE + 4U)
                 ^ *(volatile uint32_t *)(UID_BASE + 8U);
    uint32_t u21 = (uid ^ (uid >> 21)) & 0x1FFFFF;
    dev_info_par.UniqueNumber = (u21 == 0U) ? 1U : u21;
  }
  dev_info_par.MFRcode = 2046;	/* 2046 = ueblicher DIY/Open-Source-Code (6 ist ein registrierter Hersteller) */
  dev_info_par.DeviceFunction = 170; //150
  dev_info_par.DeviceClass = 80; // 75
  /* devInstance kommt jetzt aus dem EEPROM (get_param_eeprom, Byte 31) */
  dev_info_par.sysInstance = 0;
  dev_info_par.indGroup = 4;

  /* Fix: fluidType und cap kommen jetzt aus dem EEPROM (get_param_eeprom oben)
   * und werden nicht mehr hart ueberschrieben. */

  NMEA2000_config(&hfdcan1,dev_info_par.srcAdr);
  init_p_struct(&p_info);
  NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
  claim_time = HAL_GetTick();	/* nach dem Claim 250 ms keine Daten-PGNs senden */



  /* Watchdog (IWDG): LSI/64, Reload 4095 -> ca. 8 s Timeout. Faengt haengende
   * Zustaende ab (Error_Handler, blockierte Peripherie/Busse) und loest dann
   * einen Reset aus. Wird in der Hauptschleife aufgefrischt und laeuft nach
   * dem Start unabschaltbar (Register-Zugriff, kein HAL-IWDG-Modul noetig). */
  IWDG->KR  = 0x0000CCCCUL;	/* Watchdog starten */
  IWDG->KR  = 0x00005555UL;	/* Register-Zugriff freigeben */
  IWDG->PR  = 0x04UL;			/* Prescaler /64 -> ~500 Hz */
  IWDG->RLR = 4095UL;			/* max. Reload -> ~8,2 s */
  {	/* warten bis die Register uebernommen sind (mit Timeout) */
  	uint32_t iwdg_t0 = HAL_GetTick();
  	while ((IWDG->SR != 0U) && ((HAL_GetTick() - iwdg_t0) < 50U)) {}
  }
  IWDG->KR  = 0x0000AAAAUL;	/* Zaehler laden */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* Hauptschleife (kooperatives, zeitscheibenbasiertes Scheduling ueber
   * HAL_GetTick-Vergleiche - kein RTOS). Pro Durchlauf, jeweils per eigenem
   * Timer gedrosselt:
   *   - Messtakt (tx_time): Druck lesen, EMA-filtern, Fuellstand rechnen, DAC
   *   - NMEA2000 senden: Fluid Level 127505 (2,5 s), Temperatur 130312 (2 s),
   *     Heartbeat 126993 (60 s); auf Anfrage Adressen-Claim/PGN-Liste
   *   - reassemblierte Fast-Packets abarbeiten (126208/126720/65240)
   *   - BLE: empfangene Kommandos, Status-Streaming, aufgeschobener Namens-Set,
   *     einmaliger Boot-Abgleich (Name + Security-Provisionierung)
   *   - LED-Animation
   * Der Watchdog wird zu Beginn jedes Durchlaufs gefuettert. */
  while (1)
  {
	  IWDG->KR = 0x0000AAAAUL;	/* Watchdog fuettern */
	  time_el = HAL_GetTick();




	  	if((time_el-last_run)>=tx_time)
	  	{
	  		last_run = time_el;

	  		error_mode &= ~ERROR_I2C;

	  		/* Bus-Off-Anzeige zuruecknehmen, sobald der Controller wieder auf dem
	  		 * Bus ist (Recovery wird in HAL_FDCAN_ErrorStatusCallback angestossen) */
	  		{
	  			FDCAN_ProtocolStatusTypeDef psr;
	  			if ((HAL_FDCAN_GetProtocolStatus(&hfdcan1, &psr) == HAL_OK) && (psr.BusOff == 0U))
	  			{
	  				error_mode &= ~ERROR_TX_CAN;
	  			}
	  		}

	  		sensor_data_rx = get_value();
	  		press_unfilt = sensor_data_rx.pressure;	/* Rohwert fuer die App (STAT: P=) */

	  		/* EMA-Filter: 'wertung'/1000 = Anteil des ALTEN gefilterten Werts.
	  		 * Werkswert 50 -> 95 % neuer Messwert, praktisch ungefiltert;
	  		 * per BLE "FILT 0..990" einstellbar, z.B. 900 -> Zeitkonstante ~1 s
	  		 * bei 100 ms Messtakt (gegen schwappenden Tank). */
	  		raw_press = (int32_t)(((int64_t)sensor_data_rx.pressure * (1000-wertung) + (int64_t)raw_press*wertung)/1000);
	  		sensor_data_rx.pressure = (int32_t)raw_press;

	  		if(run_mode == 1){
	  			/* Fuellhoehe (linear aus Druck) -> Volumen ueber Stuetzstellen-Tabelle */
	  			percent_val = linearize_percent(calc_percent(&EEPROM_values, raw_press));
	  			set_volt(percent_val, &DAC_EEPROM_values);
	  			switch (setup_mode) {
	  				case 1: if (press_unfilt>=100){	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz);
	  							   ungefiltert wie CAL100 - siehe ble_app.c */
	  							EEPROM_values.max_val = press_unfilt/100;
	  							EEPROM_values.calib_available = 0x00;
	  							save_EEPROM(&EEPROM_values);
	  						}
	  						setup_mode = 0;
	  						break;
	  				case 2:	setup_mode = 0;
	  						EEPROM_values.calib_available = 0xFF;
	  						EEPROM_values.max_val = std_press;
	  						EEPROM_values.offset = std_offset;
	  						save_EEPROM(&EEPROM_values);
	  						break;
	  				default: 	setup_mode = 0;
	  							break;
	  			}
	  		}
	  	}

	  	//Hier Senderoutine einfügen für NMEA2000

	  	/* Sendezyklen nach NMEA2000-Norm: 127505 alle 2,5 s, 130312 alle 2 s,
	  	 * Heartbeat 126993 alle 60 s. Vorher wurde alles im 200-ms-Takt gesendet
	  	 * (~12-fache Buslast ohne Nutzen). */
	  	if((hw_otp_mismatch == 0) && ((time_el-last_run_nmea)>=nmea_time) && ((time_el-claim_time)>=250))
	  	{
	  		last_run_nmea = time_el;
	  		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, percent_val, dev_info_par.cap);
	  	}
	  	if((hw_otp_mismatch == 0) && ((time_el-last_run_temp)>=temp_time) && ((time_el-claim_time)>=250))
	  	{
	  		last_run_temp = time_el;
	  		NMEA2000_SendTemperature(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, TEMP_SOURCE_NMEA, sensor_data_rx.temp);
	  	}
	  	if(((time_el-last_run_hb)>=hb_time) && ((time_el-claim_time)>=250))
	  	{
	  		last_run_hb = time_el;
	  		NMEA2000_SendHeartbeat(&hfdcan1, dev_info_par.srcAdr, (uint16_t)hb_time);
	  	}
	  	if(fluid_req != 0)	/* ISO Request auf 127505 -> sofort antworten */
	  	{
	  		fluid_req = 0;
	  		if (hw_otp_mismatch == 0)
	  		{
	  			NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, percent_val, dev_info_par.cap);
	  			last_run_nmea = time_el;
	  		}
	  	}
	  	if(pgnlist_req != 0)	/* ISO Request auf 126464 -> TX/RX-PGN-Listen senden */
	  	{
	  		pgnlist_req = 0;
	  		NMEA2000_SendPGNList(&hfdcan1, dev_info_par.srcAdr);
	  	}
	  	if(adr_lost != 0)
	  	{
	  		adr_lost = 0;
	  		/* Arbitrierung verloren: auf naechste Adresse im dynamischen
	  		 * Bereich (128..251) ausweichen, Filter umkonfigurieren,
	  		 * neu claimen und die Adresse persistieren. */
	  		uint8_t new_adr = dev_info_par.srcAdr + 1;
	  		if((new_adr < 128) || (new_adr > 251))
	  		{
	  			new_adr = 128;
	  		}
	  		dev_info_par.srcAdr = new_adr;
	  		NMEA2000_change_address(&hfdcan1, new_adr);
	  		NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
	  		claim_time = time_el;
	  		set_adr_eeprom(new_adr);	/* schreibt nur bei tatsaechlicher Aenderung */
	  	}

	  	if(adr_claim != 0)
	  	{
	  		adr_claim = 0;
	  		NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
	  	}

	  	if(prod_info != 0)
	  	{
	  		prod_info = 0;
	  		NMEA2000_setPInfo(&hfdcan1, &p_info, dev_info_par.srcAdr);
	  	}
	  	if(boot_cfginfo_pending && ((time_el - claim_time) >= 300))
	  	{
	  		/* Einmalig nach dem Boot (nach der Claim-Sendepause) den aktuellen
	  		 * Namensstand als 126998 verschicken. So sehen PC-Tool/Plotter
	  		 * z.B. nach einem Werksreset sofort, dass der Name weg ist -
	  		 * ohne selbst anfragen zu muessen. */
	  		boot_cfginfo_pending = 0;
	  		dev_info++;
	  	}
	  	if(dev_info != 0)
	  	{
	  		dev_info = 0;
	  		NMEA2000_setDevInfo(&hfdcan1, dev_info_par.srcAdr, sensor_name);
	  	}

	  	if(gf_ready != 0)
		{
	  		/* Fast-Packet-Nachricht vollstaendig empfangen ->
	  		 * in der Hauptschleife abarbeiten */
	  		if (gf_pgn == 126208)
	  		{
	  			handle_group_function();
	  		}
	  		else if (gf_pgn == 126720)
	  		{
	  			handle_prop_config();
	  		}
	  		else if (gf_pgn == 65240)
	  		{
	  			handle_commanded_address();
	  		}
	  		gf_ready = 0;
		}

		//########## BLE (Proteus-e) ##########
		if(ble_data_ready != 0)
		{
			ble_handle_command(ble_data_buf, ble_data_len);
			ble_data_ready = 0;		/* Puffer wieder freigeben */
		}

		/* --- Bond-Selbstheilung: gescheiterte Verbindungen erkennen --- */
		if (!ble_prev_conn && ble_connected)	/* neue Verbindung beginnt */
		{
			/* ble_sec_state setzt jetzt die ISR bei CMD_CONNECT_IND zurueck.
			 * Hier waere es ein Wettlauf: eine frueh eintreffende
			 * CMD_SECURITY_IND wuerde wieder verworfen. */
			ble_sec_seen = 0;
			last_run_ble = time_el;	/* erster Status erst eine Periode spaeter */
		}
		if (ble_connected && ble_channel_open)
		{
			ble_chan_seen = 1;
		}
		if (ble_connected && (ble_sec_state != 0xFF))
		{
			ble_sec_seen = 1;
		}
		if (ble_prev_conn && !ble_connected)	/* Verbindung ist zu Ende */
		{
			if (ble_chan_seen || ble_sec_seen)
			{
				/* Kanal ging auf ODER das Pairing lief sauber durch. In
				 * beiden Faellen sind die Bonds in Ordnung - bricht die
				 * Verbindung danach ab, liegt es an der App und die Bonds
				 * duerfen auf keinen Fall geloescht werden (sonst kippt
				 * die Kopplung staendig hin und her). */
				ble_fail_cnt = 0;
				if (ble_chan_seen)
				{
					bond_heal_cnt = 0;	/* alles gut -> Heilung wieder frei */
				}
			}
			else if (ble_fail_cnt < 255)
			{
				ble_fail_cnt++;
			}
			ble_chan_seen = 0;
			ble_sec_seen = 0;

			if ((ble_fail_cnt >= 3) && (bond_heal_cnt < BOND_HEAL_MAX)
					&& (bond_heal_step == 0) && (ble_sync_step == 2))
			{
				/* Pairing-Sackgasse -> Modul-Bonds loeschen, dann Reset.
				 * BEWUSST ohne Marker-Bedingung: gerade nach einer
				 * abgebrochenen Provisionierung (Marker leer) haelt das
				 * Modul noch alte Bonds - genau dann muss die Heilung
				 * greifen. Nur nicht mitten in eine laufende Kette funken
				 * (ble_sync_step == 2 = Kette gerade nicht aktiv). */
				bond_heal_cnt++;
				ble_fail_cnt = 0;	/* naechste Heilung braucht 3 neue Fehler */
				bond_heal_step = 1;
				bond_heal_t0 = time_el;
				BLE_DeleteBonds();
			}
		}
		ble_prev_conn = ble_connected;

		/* Zustandsbild fuer die NMEA2000-Diagnose nachfuehren. Kostet nichts
		 * und ist im Fehlerfall die einzige Moeglichkeit, an diese Werte zu
		 * kommen - ueber BLE geht dann ja gerade nichts. */
		ble_diag.sync_step   = ble_sync_step;
		ble_diag.secprov_sub = secprov_sub;
		ble_diag.secprov_att = secprov_att;
		ble_diag.marker      = cfg_data[CFG_SECPROV_OFF];
		ble_diag.heal_cnt    = bond_heal_cnt;
		ble_diag.fail_cnt    = ble_fail_cnt;

		/* --- Einmalig zuruecklesen, was tatsaechlich im Modul steht ---
		 * Sicherheitsmodus, statische Passkey und Modul-Firmware. Ohne diese
		 * drei Werte ist jede Aussage ueber eine gescheiterte Kopplung
		 * geraten: erst sie zeigen, ob das Modul ueberhaupt im Passkey-
		 * Bonding-Modus steht, ob die dort hinterlegte PIN die des Sensors
		 * ist und welche Modul-Firmware ueberhaupt laeuft. Nur im getrennten
		 * Zustand - CMD_GET_REQ ist zwar harmlos, aber waehrend einer
		 * Verbindung wird am Modul grundsaetzlich nichts angefasst. */
		if ((ble_sync_step >= 2) && (ble_rb_step < 6) && (ble_connected == 0)
				&& (bonds_req == 0) && (time_el >= ble_rb_next))
		{
			static const uint8_t rb_idx[3] = { CFG_IDX_SECFLAGS,
											   CFG_IDX_STATICPASSKEY, 0x01 };
			uint8_t slot = (uint8_t)(ble_rb_step >> 1);

			if ((ble_rb_step & 1U) == 0U)		/* Abfrage schicken */
			{
				if (BLE_RequestSetting(rb_idx[slot]))
				{
					ble_rb_step++;
					ble_rb_t0 = time_el;
					ble_rb_next = time_el + 20;
				}
				else
				{
					ble_rb_step = 6;	/* UART tot -> gar nicht erst weiter */
				}
			}
			else								/* auf die Antwort warten */
			{
				if (ble_get_ready && (ble_get_index == rb_idx[slot]))
				{
					if (slot == 0)
					{
						ble_diag.secflags = ble_get_value[0];
						ble_diag.rb_flags |= 0x01;
					}
					else if (slot == 1)
					{
						uint16_t n = (ble_get_len > BLE_PIN_LEN)
									 ? (uint16_t)BLE_PIN_LEN : ble_get_len;
						memcpy(ble_diag.passkey, ble_get_value, n);
						ble_diag.rb_flags |= 0x02;
					}
					else if (ble_get_len >= 3)
					{
						/* 3 Bytes: Patch, Minor, Major - wie in der
						 * BONDS-Diagnose ueber BLE */
						ble_diag.modfw[0] = ble_get_value[2];
						ble_diag.modfw[1] = ble_get_value[1];
						ble_diag.modfw[2] = ble_get_value[0];
						ble_diag.rb_flags |= 0x04;
					}
					ble_get_ready = 0;
					ble_rb_step++;
					ble_rb_next = time_el + 100;
				}
				else if ((time_el - ble_rb_t0) > 1000)
				{
					/* Keine Antwort - der naechste Wert ist trotzdem einen
					 * Versuch wert; das fehlende rb_flags-Bit sagt aus, dass
					 * dieser Wert unbekannt ist. */
					ble_rb_step++;
					ble_rb_next = time_el + 100;
				}
				else
				{
					ble_rb_next = time_el + 20;
				}
			}
		}

		if (bond_heal_step == 1)
		{
			if (ble_connected)
			{
				/* Inzwischen haengt wieder ein Handy dran. Der Reset wuerde es
				 * mitten im neuen Pairing rauswerfen - also genau den Fehler
				 * erzeugen, den die Heilung beheben soll. Die Bonds sind
				 * bereits geloescht, das ist der wirksame Teil. */
				bond_heal_step = 0;
			}
			else if ((ble_delbonds_cnf != 0) || ((time_el - bond_heal_t0) > 800))
			{
				bond_heal_step = 0;
				BLE_ResetModule();	/* frisch starten, Handy koppelt danach neu */
				if (cfg_data[CFG_SECPROV_OFF] != CFG_SECPROV_MAGIC)
				{
					/* Provisionierung ist noch offen (z. B. Kette abgebrochen):
					 * direkt einen frischen Anlauf starten - das Modul bootet
					 * gerade, die Kette hat freie Bahn. */
					ble_sync_step = 1;
					secprov_sub = 0;
					secprov_wait = 0;
					secprov_att = 0;
					secprov_ints = 0;
					secprov_rounds = 0;
					ble_sync_next = time_el + 1500;
				}
			}
		}

		if (bonds_req == 1)			/* BONDS-Diagnose: Tabelle anfragen */
		{
			if (BLE_RequestBonds())
			{
				bonds_req = 2;
				bonds_t0 = time_el;
			}
			else
			{
				bonds_req = 0;
				BLE_SendString("ERR BONDS\n");
			}
		}
		else if (bonds_req == 2)	/* auf CMD_GETBONDS_CNF warten */
		{
			if (ble_bonds_ready == 1)
			{
				char bl[32];
				snprintf(bl, sizeof(bl), "BONDS;%u;SEC=%u\n",
						 (unsigned)ble_bonds_count, (unsigned)ble_sec_state);
				BLE_SendString(bl);
				bonds_req = 3;	/* zusaetzlich Modul-FW melden */
			}
			else if (ble_bonds_ready == 2)
			{
				/* Modul hat geantwortet, aber mit Fehlerstatus (z. B. 255 =
				 * Kommando dieser Modul-Firmware unbekannt) */
				char bl[24];
				snprintf(bl, sizeof(bl), "ERR BONDS st=%u\n",
						 (unsigned)ble_bonds_status);
				BLE_SendString(bl);
				bonds_req = 3;
			}
			else if ((time_el - bonds_t0) > 1500)
			{
				BLE_SendString("ERR BONDS timeout\n");
				bonds_req = 3;
			}
		}
		else if (bonds_req == 3)	/* Modul-Firmware-Version abfragen */
		{
			if ((ble_sync_step >= 2) && BLE_RequestSetting(0x01))	/* FS_FWVersion */
			{
				bonds_req = 4;
				bonds_t0 = time_el;
			}
			else
			{
				bonds_req = 0;
			}
		}
		else if (bonds_req == 4)	/* auf die GET-Antwort warten */
		{
			if (ble_get_ready && (ble_get_index == 0x01))
			{
				char bl[40];
				/* 3 Bytes: Patch, Minor, Major */
				if (ble_get_len >= 3)
				{
					snprintf(bl, sizeof(bl), "MODFW;%u.%u.%u\n",
							 (unsigned)ble_get_value[2],
							 (unsigned)ble_get_value[1],
							 (unsigned)ble_get_value[0]);
				}
				else
				{
					snprintf(bl, sizeof(bl), "MODFW;?\n");
				}
				BLE_SendString(bl);
				ble_get_ready = 0;
				bonds_req = 0;
			}
			else if ((time_el - bonds_t0) > 1500)
			{
				bonds_req = 0;
			}
		}
		if(ble_channel_open && ((time_el - last_run_ble) >= ble_time))
		{
			last_run_ble = time_el;
			ble_send_status();
		}
		if(ble_setname_pending && (ble_connected == 0))
		{
			/* Verbindung ist getrennt -> Modul ist jetzt im Leerlauf und
			 * akzeptiert die Namensänderung (CMD_SET_REQ). */
			HAL_Delay(50);
			BLE_ApplyPendingName();
		}

		/* --- Nach dem Boot: (0) Modulnamen abgleichen, (1) die Sicherheit
		 * EINMALIG provisionieren. Sicherheitsmodus und PIN werden danach im
		 * Betrieb NICHT mehr angefasst - die PIN aendert nur noch das aktive
		 * PIN-Kommando. Frueher wurde die Modul-PIN bei jedem Boot zurueck-
		 * gelesen und verglichen; liefert das Modul die PIN nicht identisch
		 * zurueck, fuehrte das zu staendigem Neu-Schreiben samt Bond-Loeschung
		 * -> Kopplung nach jedem Neustart weg. Deshalb jetzt einmalig. */
		if ((ble_sync_step < 2) && (time_el >= ble_sync_next))
		{
			if (ble_sync_step == 0)			/* Modulname (per GET abgleichen) */
			{
				if (ble_sync_wait == 0)
				{
					if (BLE_RequestSetting(CFG_IDX_DEVICENAME))
					{
						ble_sync_wait = 1;
						ble_sync_next = time_el + 500;
					}
					else
					{
						ble_sync_step = 1;	/* UART-Fehler -> Name ueberspringen */
						ble_sync_next = time_el + 100;
					}
				}
				else if (ble_get_ready && (ble_get_index == CFG_IDX_DEVICENAME))
				{
					char want[21];
					uint32_t settle = 100;
					ble_desired_name(want);
					if ((ble_get_len != strlen(want))
							|| (memcmp(ble_get_value, want, ble_get_len) != 0))
					{
						BLE_SetDeviceName(want);	/* behandelt 'verbunden' selbst */
						settle = 2000;				/* Modul startet neu */
					}
					ble_get_ready = 0;
					ble_sync_wait = 0;
					ble_sync_tries = 0;
					ble_sync_step = 1;
					ble_sync_next = time_el + settle;
				}
				else if (++ble_sync_tries >= 3)	/* keine Antwort -> weiter */
				{
					ble_sync_wait = 0;
					ble_sync_tries = 0;
					ble_sync_step = 1;
					ble_sync_next = time_el + 100;
				}
				else
				{
					ble_sync_wait = 0;
					ble_sync_next = time_el;
				}
			}
			else							/* (1) Sicherheit einmalig provisionieren */
			{
				if (cfg_data[CFG_SECPROV_OFF] == CFG_SECPROV_MAGIC)
				{
					ble_sync_step = 2;	/* schon provisioniert -> nie wieder anfassen */
				}
				else if (secprov_wait == 0)	/* naechstes Kommando der Kette senden */
				{
					if (ble_connected)
					{
						/* Solange ein Handy verbunden ist, wird am Modul
						 * NICHTS geaendert - auch nicht der Reset aus
						 * Schritt 0. Jedes CMD_SET_REQ und jeder Reset laesst
						 * den Proteus-e neu starten, und zwar ohne
						 * Trennungsmeldung: das Handy sieht den Sensor mitten
						 * im Pairing verschwinden (LINK_SUPERVISION_TIMEOUT),
						 * die Kopplung kommt nie zustande und die naechsten
						 * Versuche laufen genauso. Die Kette wartet deshalb
						 * einfach ab; ihr eigentliches Fenster sind ohnehin
						 * die ersten Sekunden nach dem Einschalten, bevor
						 * sich ein Handy verbinden kann. Eigener Zaehler,
						 * weil ble_sync_tries an anderen Stellen genullt
						 * wird. */
						ble_sync_next = time_el + 200;
						if (++secprov_ints > 150)	/* ~30 s ununterbrochen */
						{
							secprov_ints = 0;
							if (!ble_chan_seen && !ble_sec_seen)
							{
								/* Diese Verbindung bringt nichts: weder geht
								 * der Datenkanal auf noch wird gekoppelt.
								 * Einmal sauber trennen (CMD_DISCONNECT_REQ,
								 * kein Reset) - das Handy sieht ein normales
								 * Verbindungsende, und die Kette kommt dran. */
								BLE_Disconnect();
							}
							else if (++secprov_rounds >= 3)
							{
								/* Es wird gekoppelt oder gearbeitet, nur sehr
								 * lange. Dann hat die Provisionierung Zeit bis
								 * zum naechsten Boot - Vorrang hat die
								 * laufende Verbindung. */
								ble_sync_step = 2;
							}
						}
					}
					else
					{
						uint8_t sent;
						char pin[BLE_PIN_LEN + 1];

						switch (secprov_sub)
						{
						case 0:
							/* Das Modul ZUERST neu starten. Solange es bootet,
							 * kann sich kein Handy verbinden, und gleich nach
							 * der Boot-Meldung hat die Kette freie Bahn fuer
							 * das erste CMD_SET_REQ. Laeuft nur im getrennten
							 * Zustand (siehe oben) - sonst wuerde ausgerechnet
							 * dieser Schritt die Verbindung abwuergen, die er
							 * absichern soll. */
							sent = BLE_ResetModule();
							break;
						case 1:
							sent = BLE_SetSecFlags(BLE_SECFLAGS_TARGET);
							break;
						case 2:
							get_pin_eeprom(pin);
							sent = BLE_SetPasskey(pin);
							break;
						case 3:
							sent = BLE_DeleteBonds();
							break;
						default:
							sent = BLE_ResetModule();
							break;
						}
						if (sent)
						{
							secprov_wait = 1;
							ble_sync_tries = 0;
						}
						ble_sync_next = time_el + 100;
					}
				}
				else						/* auf Bestaetigung/Modul-Neustart warten */
				{
					uint8_t done = 0, fail = 0;

					if (secprov_sub == 3)		/* DeleteBonds: nur CNF, kein Neustart */
					{
						done = (ble_delbonds_cnf == 1);
						fail = (ble_delbonds_cnf == 2);
					}
					else if ((secprov_sub == 0) || (secprov_sub >= 4))
					{
						done = ble_boot_seen;	/* Reset: Boot-Meldung abwarten */
					}
					else						/* SET: erst CNF, dann Modul-Neustart */
					{
						done = ((ble_set_cnf == 1) && ble_boot_seen);
						fail = (ble_set_cnf == 2);
					}

					if (done)
					{
						secprov_wait = 0;
						secprov_att = 0;
						ble_sync_tries = 0;
						if (secprov_sub >= 4)	/* Kette komplett -> Marker setzen */
						{
							cfg_data[CFG_SECPROV_OFF] = CFG_SECPROV_MAGIC;
							config_save();
							ble_sync_step = 2;
						}
						else
						{
							/* Ohne Verzoegerung weiter: das naechste Kommando
							 * soll noch in das Zeitfenster fallen, in dem das
							 * gerade neu gestartete Modul noch keine
							 * Verbindung angenommen hat. */
							secprov_sub++;
							ble_sync_next = time_el + 20;
						}
					}
					else if (fail || (++ble_sync_tries >= 30))
					{
						/* Schritt fehlgeschlagen bzw. 3 s ohne Antwort (der
						 * Proteus-e braucht nach einem Reset gut 1,5 s):
						 * denselben Schritt neu senden, nach 3 Versuchen
						 * diesen Boot aufgeben (Marker bleibt leer). */
						secprov_wait = 0;
						ble_sync_tries = 0;
						if (++secprov_att >= 3)
						{
							secprov_att = 0;
							ble_sync_step = 2;
						}
						ble_sync_next = time_el + 300;
					}
					else
					{
						/* Weiter pollen - und nach der Bootzeit zusaetzlich
						 * aktiv nachfragen. CMD_GETSTATE_CNF ist dieselbe
						 * Meldung, die das Modul nach einem Neustart von sich
						 * aus schickt; kommt sie als Antwort auf unsere
						 * Abfrage, ist das Modul genauso sicher wieder da.
						 * Ohne das haengt die ganze Kette daran, dass die
						 * spontane Boot-Meldung auch wirklich ankommt - geht
						 * sie verloren, laeuft jeder Schritt in den Timeout,
						 * die Kette gibt auf, und PIN und Bonds bleiben
						 * ungeschrieben. */
						if ((ble_boot_seen == 0) && (ble_sync_tries >= 20))
						{
							BLE_RequestState();
						}
						ble_sync_next = time_el + 100;	/* weiter pollen */
					}
				}
			}
		}


	  	if((time_el-last_run_led)>=led_time)
	  	{
	  		last_run_led = time_el;

	  		//########## normaler Messmodus -> LED-Berechnungen ###########
	  		if(run_mode == 1)
	  		{
	  			calc_color(&LED_r, &LED_g, &LED_b, percent_val);
	  			if(led_up==1)
	  			{
	  				LED_brightness=LED_brightness+4;
	  			}
	  			else if (led_up == 0)
	  			{
	  				LED_brightness=LED_brightness-4;
	  			}
	  			else if (led_up == 2)
	  			{
	  				LED_brightness = 255;
	  				LED_r=0;
	  				LED_g=0;
	  				LED_b=0;
	  				if(error_mode & ERROR_TX_CAN)
	  				{
	  					LED_b=255;
	  				}
	  				if(error_mode & ERROR_I2C)
	  				{
	  					LED_g=255;
	  				}
	  				if(error_mode & ERROR_HWV)
	  				{
	  					LED_r=255;
	  				}
	  				if(error_cnt > 100)
	  				{
	  					LED_brightness = -100;
	  					LED_r=0;
	  					LED_g=0;
	  					LED_b=0;
	  					led_up = 1;
	  					error_cnt = 0;
	  				}
	  				else if(error_cnt > 75)
	  				{
	  					LED_brightness = 255;
	  				}
	  				else if (error_cnt > 50)
	  				{
	  					LED_brightness = 0;
	  				}
	  				error_cnt++;

	  			}
	  			else if (led_up == 3)
	  			{
	  				led_time = 350;
	  				blink_LED();
	  			}


	  			if(LED_brightness>300){
	  				led_up = 0;
	  			}
	  			else if(LED_brightness < -300)
	  			{
	  				if(error_mode != 0)
	  				{
	  					led_up = 2;
	  				}
	  				else
	  				{
	  					led_up = 1;
	  				}
	  			}
	  		}

	  		//########### Setup - Mode ############

	  		else if(run_mode == 0)
	  		{
	  			if(LED_brightness < 127)
	  			{
	  				LED_brightness = 255;
	  			}
	  			else
	  			{
	  				LED_brightness = 0;
	  			}

	  			if((time_el-sm_started)>10000)
	  			{
	  				setup_mode = 0;
	  				run_mode = 1;
	  				led_time = 20;
	  				LED_brightness=0;
	  			}

	  			switch (setup_mode) {
	  				case 0: LED_r = 0;
	  						LED_g = 255;
	  						LED_b = 0;
	  						break;
	  				case 1: LED_r = 255;
	  						LED_g = 255;
	  						LED_b = 0;
	  						break;
	  				case 2:
	  						LED_r = 255;
	  						LED_g = 0;
	  						LED_b = 0;
	  						break;
	  				default: 	setup_mode = 0;
	  							LED_r = 0;
	  							LED_g = 0;
	  							LED_b = 0;
	  							break;
	  			}
	  		}


	  		// ############ Farbe der LED am Ausgang setzen ###################
	  		set_led(LED_r, LED_g, LED_b, LED_brightness);	// Farbe der LED setzen
	  	}

#ifndef DEBUG
	  	/* Bis zum naechsten Interrupt schlafen - spaetestens der SysTick weckt
	  	 * nach 1 ms. Alle Zeitscheiben sind tickbasiert, funktional aendert
	  	 * sich nichts, nur die Stromaufnahme aus dem Bus sinkt. Im Debug-
	  	 * Build aus, damit der Debugger freie Bahn hat. */
	  	__WFI();
#endif
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = ENABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 4;
  hfdcan1.Init.NominalSyncJumpWidth = 4;
  hfdcan1.Init.NominalTimeSeg1 = 11;
  hfdcan1.Init.NominalTimeSeg2 = 4;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 2;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00303D5B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 2000;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 32000;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, BLE_MODE_Pin|D_OUT_Pin|CAN_STBY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(WC_N_GPIO_Port, WC_N_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : BLE_MODE_Pin D_OUT_Pin CAN_STBY_Pin */
  GPIO_InitStruct.Pin = BLE_MODE_Pin|D_OUT_Pin|CAN_STBY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BLE_RESET_Pin WC_N_Pin */
  GPIO_InitStruct.Pin = BLE_RESET_Pin|WC_N_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BLE_BUSY_Pin BLE_LED_Pin */
  GPIO_InitStruct.Pin = BLE_BUSY_Pin|BLE_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : TASTER_Pin */
  GPIO_InitStruct.Pin = TASTER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TASTER_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* Pins nachziehen, die auf der V1-Platine anders liegen als in der
   * generierten Init (siehe board_pins.h). Fuer die V2-Platine leer. */
  board_gpio_fixup();

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */


//********************* Interrupt Callbacks **********************


/* Bus-Off (z.B. Kurzschluss oder massive Stoerung auf dem Bus): Fehler-LED
 * setzen und die Recovery anstossen. Durch Loeschen des INIT-Bits wartet der
 * FDCAN die vorgeschriebenen 128 x 11 rezessiven Bits ab und nimmt danach
 * automatisch wieder am Busverkehr teil. Vorher gab es keine Behandlung:
 * nach einem Bus-Off sendete das Geraet nie wieder. */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
	if (ErrorStatusITs & FDCAN_IT_BUS_OFF)
	{
		FDCAN_ProtocolStatusTypeDef psr;
		if (HAL_FDCAN_GetProtocolStatus(hfdcan, &psr) == HAL_OK)
		{
			if (psr.BusOff)
			{
				error_mode |= ERROR_TX_CAN;
				CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
			}
		}
	}
}


void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
	UNUSED(GPIO_Pin);

	HAL_TIM_Base_Stop_IT(&htim6);

	uint32_t press_cnt = TIM6->CNT;		/* Fix: CNT einmal lesen, && statt & */
	if((press_cnt > 1500) && (press_cnt < 4500))
	{

		sm_started =  HAL_GetTick();
		if(run_mode == 1)
		{
			LED_brightness = 250;
			led_up = 3;
			level_led = raw_press/1000;
			if(level_led < 0)
			{
				level_led = 0;
			}
		}
		else{
			setup_mode++;
		}
	}

	TIM6->CNT = 1;

}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{

	UNUSED(GPIO_Pin);
	TIM6->CNT = 1;


	HAL_TIM_Base_Start_IT(&htim6);

}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance != TIM6)		/* Fix: Instanz pruefen */
	{
		return;
	}

	HAL_TIM_Base_Stop_IT(&htim6);

	if(run_mode == 1)
	{
		led_time = 250;
		run_mode = 0;
		sm_started =  HAL_GetTick();
	}
	else if(run_mode == 0)
	{
		led_time = 20;
		LED_brightness=0;
		run_mode = 1;
		//setup_mode = 0;
	}
}


//************* USer Functions **************

/* Config-/EEPROM-Helfer (check/get/save_EEPROM, *_dac_EEPROM,
 * set/get_param_eeprom): siehe app_config.c */


/* handle_group_function(), handle_prop_config(), handle_commanded_address():
 * siehe nmea_app.c */
/* get_adr_eeprom(), set_adr_eeprom(): siehe app_config.c */


void set_led(int32_t red, int32_t green, int32_t blue, int32_t brightness)
{
	if (red < 0){
		red = 0;
	}
	else if (red > 255){
		red = 255;
	}

	if (green < 0){
		green = 0;
	}
	else if (green > 255){
		green = 255;
	}

	if (blue < 0){
		blue = 0;
	}
	else if (blue > 255){
		blue = 255;
	}

	if (brightness < 0){
		brightness = 0;
	}
	else if (brightness > 255){
		brightness = 255;
	}

	PWM_RED = red*brightness;
	PWM_GREEN = green*brightness;
	PWM_BLUE = blue*brightness;
}

/* set_volt(), set_volt_raw(), calc_percent(): siehe sensor_common.c */

void calc_color(int32_t *c_red, int32_t *c_green, int32_t *c_blue, uint16_t percent)
{

	uint16_t nPercent, pPercent;

//########## Funktion für RGB Farbverlauf ###########

	if (percent <= 5000)
	{
		nPercent = 5000-percent;
		*c_green=0;
		*c_red = (255 * nPercent)/5000;
		*c_blue = (255 * percent)/5000;

	}
	else
	{
		pPercent = percent-5000;
		nPercent = 5000-pPercent;
		*c_red = 0;
		*c_green= (255 * pPercent)/5000;
		*c_blue = (255 * nPercent)/5000;
	}

/*
	nPercent = 10000 - percent;
	*c_blue = 0;
	*c_red = (255 * nPercent) / 10000;
	*c_green = (255 * percent) / 10000;
*/

}



void blink_LED()
{
	//100er
	if (LED_brightness <= 0)
	{
		LED_brightness = 255;

		if(level_led>99)
		{
			LED_r = 255;
			LED_g = 0;
			LED_b = 0;
			if (level_led <= 199)
			{
				led_jump = 1;
			}
			level_led = level_led - 100;
		}
		else if(level_led > 9)
		{
			LED_g = 255;
			LED_r = 0;
			LED_b = 0;
			if (level_led <= 19)
			{
				led_jump = 1;
			}
			level_led = level_led - 10;
		}
		else if(level_led > 0)
		{
			LED_b = 255;
			LED_r = 0;
			LED_g = 0;
			if (level_led <= 1)
			{
				led_jump = 1;
			}
			level_led = level_led - 1;
		}
		else if(level_led == 0)
		{
			level_led = level_led - 1;
			LED_r = 255;
			LED_g = 255;
			LED_b = 255;
		}
		else
		{
			led_up = 1;
			led_time = 20;
			LED_brightness = -100;
		}
	}
	else
	{
		if(led_jump == 1)
		{
			led_time = 850;
			led_jump = 0;
		}

		LED_brightness = 0;
		LED_r = 0;
		LED_g = 0;
		LED_b = 0;
	}
	// Weiß blinken für 0 anzeigen
}

/* get_value(), init_Sensor(): siehe sensor_legacy.c / sensor_pdms.c */

/* ble_send_status(), ble_send_lin(), ble_handle_command(): siehe ble_app.c */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
