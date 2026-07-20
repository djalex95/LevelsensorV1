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
  *   - Sensor/Mess-Ebene ................... ausgelagert nach sensor.c
  *   - LED ................................. set_led, calc_color, blink_LED
  *   - NMEA2000-Handler .................... ausgelagert nach nmea_app.c
  *   - BLE-Kommando-Ebene .................. ble_send_status, ble_send_lin,
  *                                            ble_handle_command, ble_desired_name
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
#include "config_store.h"
#include "dfu_common.h"
#include "version.h"
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
 * NMEA2000-Handler (handle_*): siehe nmea_app.h */
void ble_desired_name(char *buf);	/* auch von nmea_app.c genutzt */
void set_led(int32_t red, int32_t green, int32_t blue, int32_t brightness);

void ble_send_status();
void ble_handle_command(const uint8_t *data, uint16_t len);
void calc_color(int32_t *c_red, int32_t *c_green, int32_t *c_blue, uint16_t percent);
void blink_LED();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* volatile: wird in Interrupt-Callbacks UND Hauptschleife verwendet */
volatile uint8_t error_mode = 0;
uint8_t led_jump = 0;

volatile uint8_t run_mode = 1;
 volatile uint8_t setup_mode = 0;
volatile int32_t raw_press = 0;	/* wird auch im EXTI-Callback gelesen;
									   32 bit -> atomarer Zugriff auf dem M0+ (kein Torn Read) */

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

 uint8_t wertung = 50;

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

/* Einmaliger Abgleich Modul <-> Config nach dem Boot in drei Schritten:
 * 0 = Name, 1 = Sicherheitsmodus (SecFlags), 2 = Passkey (PIN), 3 = fertig.
 * Es wird jeweils erst gelesen und nur bei ABWEICHUNG geschrieben
 * (Modul-Flash ~10k Zyklen, jedes Schreiben loest einen Modul-Neustart aus). */
static uint8_t  ble_sync_step = 0;
static uint8_t  ble_sync_wait = 0;		/* 1 = Antwort auf GET ausstehend    */
static uint32_t ble_sync_next = 1500;	/* Modul bootet ~1,5 s               */
static uint8_t  ble_sync_tries = 0;

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

  if (check_EEPROM())
  {
	  get_EEPROM(&EEPROM_values);
  }
  else{
	  EEPROM_values.max_val = std_press;
	  EEPROM_values.offset = std_offset;
  }

  get_param_eeprom(&dev_info_par, &device_param);
  get_name_eeprom(sensor_name);


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

	  		/* EMA-Filter: 'wertung'/1000 = Anteil des ALTEN gefilterten Werts.
	  		 * wertung=50 -> 95 % neuer Messwert, 5 % alter Wert (Verhalten wie bisher,
	  		 * fuer staerkere Glaettung wertung erhoehen, z.B. 900). */
	  		raw_press = (int32_t)(((int64_t)sensor_data_rx.pressure * (1000-wertung) + (int64_t)raw_press*wertung)/1000);
	  		sensor_data_rx.pressure = (int32_t)raw_press;

	  		if(run_mode == 1){
	  			/* Fuellhoehe (linear aus Druck) -> Volumen ueber Stuetzstellen-Tabelle */
	  			percent_val = linearize_percent(calc_percent(&EEPROM_values, raw_press));
	  			set_volt(percent_val, &DAC_EEPROM_values);
	  			switch (setup_mode) {
	  				case 1: if (raw_press>=100){	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz) */
	  							EEPROM_values.max_val = raw_press/100;
	  							EEPROM_values.calib_available = 0x00;
	  							save_EEPROM(&EEPROM_values);
	  						}
	  						setup_mode = 0;
	  						break;
	  				case 2:	setup_mode = 0;
	  						EEPROM_values.calib_available = 0xFF;
	  						save_EEPROM(&EEPROM_values);
	  						EEPROM_values.max_val = std_press;
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
	  	if(((time_el-last_run_nmea)>=nmea_time) && ((time_el-claim_time)>=250))
	  	{
	  		last_run_nmea = time_el;
	  		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, percent_val, dev_info_par.cap);
	  	}
	  	if(((time_el-last_run_temp)>=temp_time) && ((time_el-claim_time)>=250))
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
	  		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, percent_val, dev_info_par.cap);
	  		last_run_nmea = time_el;
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
				else if (ble_connected)
				{
					/* nicht mitten in eine Verbindung reset-en: erst trennen,
					 * gleich erneut versuchen (begrenzt, sonst aufgeben) */
					BLE_Disconnect();
					if (++ble_sync_tries >= 5)
					{
						cfg_data[CFG_SECPROV_OFF] = CFG_SECPROV_MAGIC;
						config_save();
						ble_sync_step = 2;
					}
					else
					{
						ble_sync_next = time_el + 1500;
					}
				}
				else
				{
					BLE_ProvisionSecurity(BLE_SECFLAGS_TARGET);
					cfg_data[CFG_SECPROV_OFF] = CFG_SECPROV_MAGIC;
					config_save();
					ble_sync_step = 2;
					ble_sync_next = time_el + 2000;
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
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}


/* get_name_eeprom(): siehe app_config.c */

/* Gewuenschter BLE-Modulname (max. 20 Zeichen, Proteus-Limit):
 * der Sensorname aus dem Config ist die einzige Quelle der Wahrheit.
 * Ist keiner gesetzt (Werkszustand), gilt der Default "LevelSense-<UID>"
 * aus der NMEA2000 Unique Number - damit ist jede Platine ab Werk
 * eindeutig unterscheidbar. buf braucht mind. 21 Bytes. */
void ble_desired_name(char *buf)
{
	if (sensor_name[0] != '\0')
	{
		strncpy(buf, sensor_name, 20);
		buf[20] = '\0';
	}
	else
	{
		/* 21-bit Unique Number -> max. 6 Hex-Zeichen, gesamt <= 17 Zeichen */
		snprintf(buf, 21, "LevelSense-%05lX", (unsigned long)dev_info_par.UniqueNumber);
	}
}

/* set_name_eeprom(): siehe app_config.c */

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
  huart2.Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
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
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BLE_MODE_GPIO_Port, BLE_MODE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BLE_RESET_GPIO_Port, BLE_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin : BLE_MODE_Pin */
  GPIO_InitStruct.Pin = BLE_MODE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BLE_MODE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BLE_RESET_Pin */
  GPIO_InitStruct.Pin = BLE_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BLE_RESET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BLE_BUSY_Pin BLE_LED_Pin */
  GPIO_InitStruct.Pin = BLE_BUSY_Pin|BLE_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PD2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PB8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
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

/* set_volt(), set_volt_raw(), calc_percent(): siehe sensor.c */

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

/* get_value(), init_Sensor(): siehe sensor.c */

/*
 * Sendet den aktuellen Sensorzustand als maschinenlesbare Zeile an die App.
 * Format:  STAT;L=<%>;T=<C>;F=<typ>;C=<L>;I=<inst>;CAL=<0/1>;V=<x.y.z>;HW=<rev>\n
 * L: Füllstand in %, T: Temperatur in Grad C, F: Fluidtyp (0..15),
 * C: Kapazität (Liter), I: Instanz, CAL: 1 = kalibriert.
 */
void ble_send_status()
{
	char line[96];

	/* percent_val: 100,00 % = 10000 */
	int p_int = percent_val / 100;
	int p_frac = (percent_val % 100) / 10;

	/* temp: 0,01 Grad C, Vorzeichen sauber behandeln */
	int16_t t = sensor_data_rx.temp;
	const char *tsign = (t < 0) ? "-" : "";
	int ta = (t < 0) ? -t : t;

	int cal = (EEPROM_values.calib_available == 0x00) ? 1 : 0;

	snprintf(line, sizeof(line), "STAT;L=%d.%d;T=%s%d.%02d;F=%d;C=%d;I=%d;CAL=%d;V=%s;HW=%d\n",
			 p_int, p_frac, tsign, ta / 100, ta % 100,
			 dev_info_par.fluidType, dev_info_par.cap, dev_info_par.devInstance, cal,
			 FW_VERSION, HW_REV);

	BLE_SendString(line);
}

/* Sendet die aktuelle Tankform-Kennlinie:  LIN;v0,v1,...,v10\n */
static void ble_send_lin()
{
	char line[64];
	int n = 0;
	n += snprintf(line + n, sizeof(line) - n, "LIN;");
	for (int i = 0; i < 11; i++)
	{
		n += snprintf(line + n, sizeof(line) - n, "%d%s",
					  device_param.lin_point[i], (i < 10) ? "," : "\n");
	}
	BLE_SendString(line);
}

/*
 * Verarbeitet ein Textkommando von der App (CMD_DATA_IND). Vollstaendige
 * Spezifikation in PC_Tools/BLE_Protokoll.md. Unterstützt (case-insensitive):
 *   VER            Firmware-Version senden (VER;x.y.z)
 *   GET            aktuellen Status sofort senden (STAT;...)
 *   LIN            aktuelle Tankform-Kennlinie senden (LIN;...)
 *   LIN v0,...,v10 Kennlinie setzen (11 Werte 0..100, steigend)
 *   CAL100         aktuellen Druck als 100 % kalibrieren
 *   CALRESET       Kalibrierung auf Werkswert zurücksetzen
 *   FLUID <0..15>  Fluidtyp setzen
 *   CAP <1..255>   Tankkapazität (Liter) setzen
 *   INST <0..15>   Instanz setzen
 *   NAME <text>    Sensor-/BLE-Namen setzen; NAME (ohne Arg) fragt ihn ab
 *   FACTORYRESET   Config löschen und neu starten
 *   DFU            in den Bootloader/Update-Modus wechseln
 */
void ble_handle_command(const uint8_t *data, uint16_t len)
{
	char cmd[80];
	char resp[32];
	uint16_t n = (len < sizeof(cmd) - 1) ? len : sizeof(cmd) - 1;
	memcpy(cmd, data, n);
	cmd[n] = '\0';

	/* trailing CR/LF/Leerzeichen entfernen */
	while (n > 0 && (cmd[n-1] == '\r' || cmd[n-1] == '\n' || cmd[n-1] == ' '))
	{
		cmd[--n] = '\0';
	}

	if (strncasecmp(cmd, "VER", 3) == 0)
	{
		BLE_SendString("VER;" FW_VERSION "\n");
	}
	else if (strncasecmp(cmd, "GET", 3) == 0)
	{
		ble_send_status();
	}
	else if (strncasecmp(cmd, "LIN", 3) == 0)
	{
		if (cmd[3] == '\0')				/* Abfrage der Kennlinie */
		{
			ble_send_lin();
		}
		else if (cmd[3] == ' ')			/* Kennlinie setzen: LIN v0,..,v10 */
		{
			uint8_t pts[11];
			int cnt = 0;
			char *p = cmd + 4;
			while (cnt < 11 && *p)
			{
				pts[cnt++] = (uint8_t)atoi(p);
				while (*p && *p != ',') p++;
				if (*p == ',') p++;
			}
			if (cnt == 11 && lin_table_valid(pts))
			{
				memcpy(device_param.lin_point, pts, 11);
				set_param_eeprom(&dev_info_par, &device_param);
				BLE_SendString("OK LIN\n");
			}
			else
			{
				BLE_SendString("ERR LIN\n");
			}
		}
		else
		{
			BLE_SendString("ERR ?\n");
		}
	}
	else if (strncasecmp(cmd, "CAL100", 6) == 0)
	{
		if (raw_press >= 100)	/* /100 muss max_val >= 1 ergeben (Div-durch-0-Schutz) */
		{
			EEPROM_values.max_val = raw_press / 100;
			EEPROM_values.calib_available = 0x00;
			save_EEPROM(&EEPROM_values);
			BLE_SendString("OK CAL100\n");
		}
		else
		{
			BLE_SendString("ERR CAL100 nodruck\n");
		}
	}
	else if (strncasecmp(cmd, "CALRESET", 8) == 0)
	{
		EEPROM_values.calib_available = 0xFF;
		save_EEPROM(&EEPROM_values);
		EEPROM_values.max_val = std_press;
		BLE_SendString("OK CALRESET\n");
	}
	else if (strncasecmp(cmd, "FACTORYRESET", 12) == 0)
	{
		/* Kompletten Config loeschen und neu starten (Adresse 0x21,
		 * unkalibriert, kein Name). Der BLE-Modulname faellt beim naechsten
		 * Boot per Namensabgleich auf "LevelSense-<UID>" zurueck. */
		BLE_SendString("OK FACTORYRESET\n");
		HAL_Delay(100);
		config_factory_reset();
		__disable_irq();
		NVIC_SystemReset();
	}
	else if (strncasecmp(cmd, "DFU", 3) == 0)
	{
		/* Update-Modus anfordern: Magic ins reservierte RAM schreiben und neu
		 * starten. Der Bootloader erkennt es und geht in den Empfangsmodus. */
		BLE_SendString("OK DFU\n");
		HAL_Delay(100);
		*DFU_REQ_ADDR = DFU_REQ_MAGIC;
		__disable_irq();
		NVIC_SystemReset();
	}
	else if ((strncasecmp(cmd, "NAME", 4) == 0) && (cmd[4] == '\0'))
	{
		/* Abfrage: gespeicherten Sensornamen melden (NAME;<text>).
		 * Noetig, weil BLE-Modulname und Sensorname auseinanderlaufen
		 * koennen (Name per Group Function vom Plotter gesetzt). */
		snprintf(resp, sizeof(resp), "NAME;%s\n", sensor_name);
		BLE_SendString(resp);
	}
	else if (strncasecmp(cmd, "NAME ", 5) == 0)
	{
		const char *name = cmd + 5;
		if (*name != '\0')
		{
			if (strcmp(name, sensor_name) == 0)
			{
				/* unveraendert -> kein Flash-Schreibzugriff, kein
				 * Modul-Neustart (die Verbindung bleibt bestehen) */
				BLE_SendString("OK NAME\n");
			}
			else
			{
				char want[21];
				/* Name persistent speichern -> erscheint als Installation
				 * Description in PGN 126998 (Geraeteliste am Plotter). */
				set_name_eeprom(name);
				get_name_eeprom(sensor_name);
				dev_info++;		/* aktualisiertes 126998 auf dem NMEA-Bus
								 * verschicken (Symmetrie zum Setzen per
								 * Group Function: PC-Tool/Plotter sehen den
								 * neuen Namen sofort) */
				/* erst bestätigen, dann Modul umbenennen (Modul startet danach
				 * neu und trennt die Verbindung). Der Modulname folgt dem
				 * gespeicherten (ggf. auf 24 Zeichen gekuerzten) Sensornamen. */
				BLE_SendString("OK NAME\n");
				HAL_Delay(50);
				ble_desired_name(want);
				BLE_SetDeviceName(want);
			}
		}
		else
		{
			BLE_SendString("ERR NAME\n");
		}
	}
	else if (strncasecmp(cmd, "FLUID ", 6) == 0)
	{
		int v = atoi(cmd + 6);
		if (v >= 0 && v <= 15)
		{
			dev_info_par.fluidType = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK FLUID %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR FLUID\n");
	}
	else if (strncasecmp(cmd, "CAP ", 4) == 0)
	{
		int v = atoi(cmd + 4);
		if (v >= 1 && v <= 255)
		{
			dev_info_par.cap = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK CAP %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR CAP\n");
	}
	else if (strncasecmp(cmd, "INST ", 5) == 0)
	{
		int v = atoi(cmd + 5);
		if (v >= 0 && v <= 15)
		{
			dev_info_par.devInstance = (uint8_t)v;
			set_param_eeprom(&dev_info_par, &device_param);
			snprintf(resp, sizeof(resp), "OK INST %d\n", v);
			BLE_SendString(resp);
		}
		else BLE_SendString("ERR INST\n");
	}
	else
	{
		BLE_SendString("ERR ?\n");
	}
}
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

#ifdef  USE_FULL_ASSERT
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
