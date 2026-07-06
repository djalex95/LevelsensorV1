/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "config_store.h"
#include "dfu_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct calib_data{
	uint8_t calib_availible;
	uint32_t max_val;
	int16_t offset;
}calib_data;

typedef struct dac_calib_data{
	uint8_t calib_availible;	// soll auch genutzt werden, um min und max daten am Ausgang zu sehen !
	int32_t dac_mx;
	int32_t dac_c;
}dac_calib_data;

typedef struct prod_param{
	uint8_t fluid_type;
	uint8_t tank_cap;
	uint8_t lin_point[11];
}prod_param;

typedef union int_convert{
	uint8_t small_arr[4];
	uint32_t max_val;
}int_convert;

typedef union int16_convert{
	uint8_t small_arr[2];
	uint16_t max_val;
}int16_convert;

typedef struct sensor_data{
	int32_t pressure;
	int16_t temp;
}sensor_mess;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define std_press 1000	//100,0mBar -> 100000uBar
#define std_offset 0

#define PWM_RED TIM3->CCR1
#define PWM_GREEN TIM3->CCR2
#define PWM_BLUE TIM3->CCR3

#define ERROR_TX_CAN 1
#define ERROR_I2C 10

#define TEMP_SOURCE_NMEA 2	//NMEA2000 Temperature Source: 2 = Inside Temperature

/* Proprietary-Header fuer PGN 126720: MFR-Code 2046, reserved 0x3, Industry Group 4
 * -> uint16 LE 0x9FFE -> Bytes 0xFE, 0x9F */
#define PROP_HDR_0 0xFE
#define PROP_HDR_1 0x9F
#define PROP_CMD_SET_LIN 0x01
#define PROP_CMD_GET_LIN 0x02
#define PROP_CMD_CALIB   0x03	/* aktuellen Druck als 100 % (max_val) kalibrieren */
#define PROP_CMD_RESET   0x04	/* Kalibrierung auf Werkswert zuruecksetzen */


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
uint8_t check_EEPROM();
void get_EEPROM(calib_data *values);
void save_EEPROM(calib_data *values);
uint8_t check_dac_EEPROM();
void get_dac_EEPROM(dac_calib_data *values);
void set_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values);
void get_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values);
uint8_t get_adr_eeprom();
void set_adr_eeprom(uint8_t adr);
void handle_group_function();
void handle_prop_config();
uint8_t lin_table_valid(uint8_t *t);
uint16_t linearize_percent(uint16_t raw);
void reset_EEPROM(calib_data *values);
void set_led(int32_t red, uint32_t green, int32_t blue, int32_t brightness);

void ble_send_status();
void ble_handle_command(const uint8_t *data, uint16_t len);
void set_volt(uint16_t percent, dac_calib_data * datas);
void set_volt_raw(uint16_t volt, dac_calib_data * datas);
uint16_t calc_percent(calib_data *datas, int64_t mw);
void calc_color(int32_t *c_red, int32_t *c_green, int32_t *c_blue, uint16_t percent);
void blink_LED();
sensor_mess get_value();
void init_Sensor();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* volatile: wird in Interrupt-Callbacks UND Hauptschleife verwendet */
volatile uint8_t error_mode = 0;
uint8_t led_jump = 0;

FDCAN_FilterTypeDef sFilterConfig;
 FDCAN_TxHeaderTypeDef TxHeader, TxHeader2;
// FDCAN_RxHeaderTypeDef RxHeader;
 uint8_t TxData0[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0x00, 0x11, 0x22};
 uint8_t TxData1[8];
 //uint8_t RxData[8];
 uint8_t i2cRX[4];
 uint32_t TxMailbox;

 uint8_t blink_times = 0;

 volatile uint8_t run_mode = 1;
 volatile uint8_t setup_mode = 0;
 volatile uint8_t try_tx = 0;

 uint32_t extID = 0x033;
 uint32_t mask = 0x7FC;

 volatile int64_t raw_press = 0;	/* wird auch im EXTI-Callback gelesen */

 //EEprom struct
 calib_data EEPROM_values;
 dac_calib_data DAC_EEPROM_values;
 prod_param device_param;

 int_convert int_arr;
 int16_convert int16_arr;

 uint32_t time_el = 0, last_run = 0, last_run_nmea=0;
 uint32_t tx_time = 100, nmea_time = 200;

 volatile uint8_t adr_claim = 0;
 volatile uint8_t adr_lost = 0;		/* Adress-Arbitrierung verloren (ISR -> Hauptschleife) */
 volatile uint8_t prod_info = 0;
 volatile uint8_t dev_info = 0;

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

HAL_StatusTypeDef tx_state;

sensor_mess sensor_data_rx;

NMEA_parameter_Product p_info;

NMEA_parameter_Device dev_info_par;



/* BLE (Proteus-e): Status-Streaming-Zeitpunkt */
uint32_t last_run_ble = 0, ble_time = 1000;
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


	 if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_TX_COMPLETE, 0xFFFFFFFF) != HAL_OK)
	 	 {
		 	 Error_Handler();
	 	 }


    /* Prepare Tx message Header */
     TxHeader.Identifier = 0x150;
     TxHeader.IdType = FDCAN_STANDARD_ID;
     TxHeader.TxFrameType = FDCAN_DATA_FRAME;
     TxHeader.DataLength = FDCAN_DLC_BYTES_6;
     TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
     TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
     TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
     TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
     TxHeader.MessageMarker = 0;

	TxHeader2.Identifier = 0x151;
	TxHeader2.IdType = FDCAN_STANDARD_ID;
	TxHeader2.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader2.DataLength = FDCAN_DLC_BYTES_5;
	TxHeader2.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader2.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader2.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader2.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader2.MessageMarker = 0;

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
  	  while(1)
  	  {
  		  DAC_EEPROM_values.dac_c = 0;
  		  DAC_EEPROM_values.dac_mx = 6205;	//alt:12409
  		  set_volt_raw(EEPROM_result * 16 , &DAC_EEPROM_values);
  	  }
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


  init_Sensor();
  BLE_Init(&huart2);		/* Proteus-e zurücksetzen und Empfang starten */


  dev_info_par.srcAdr = get_adr_eeprom();	/* zuletzt geclaimte Adresse, Fallback 0x21 */

  dev_info_par.UniqueNumber = 1090;
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



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  time_el = HAL_GetTick();




	  	if((time_el-last_run)>=tx_time)
	  	{
	  		last_run = time_el;

	  		error_mode &= ~ERROR_I2C;

	  		sensor_data_rx = get_value();

	  		int_arr.max_val = sensor_data_rx.pressure;

	  		TxData0[0] = int_arr.small_arr[3];
	  		TxData0[1] = int_arr.small_arr[2];
	  		TxData0[2] = int_arr.small_arr[1];
	  		TxData0[3] = int_arr.small_arr[0];

	  		int16_arr.max_val = sensor_data_rx.temp;

	  		TxData0[4] = int16_arr.small_arr[1];
	  		TxData0[5] = int16_arr.small_arr[0];

	  		/* EMA-Filter: 'wertung'/1000 = Anteil des ALTEN gefilterten Werts.
	  		 * wertung=50 -> 95 % neuer Messwert, 5 % alter Wert (Verhalten wie bisher,
	  		 * fuer staerkere Glaettung wertung erhoehen, z.B. 900). */
	  		raw_press = ((int64_t)sensor_data_rx.pressure * (1000-wertung) + raw_press*wertung)/1000;
	  		sensor_data_rx.pressure = (int32_t)raw_press;

	  		int_arr.max_val = (int32_t)raw_press;

	  		if(run_mode == 1){
	  			try_tx ++;


	  			TxData1[0] = int_arr.small_arr[3];
	  			TxData1[1] = int_arr.small_arr[2];
	  			TxData1[2] = int_arr.small_arr[1];
	  			TxData1[3] = int_arr.small_arr[0];

	  			/* Fuellhoehe (linear aus Druck) -> Volumen ueber Stuetzstellen-Tabelle */
	  			percent_val = linearize_percent(calc_percent(&EEPROM_values, raw_press));
	  			set_volt(percent_val, &DAC_EEPROM_values);
	  			TxData1[4] = (percent_val*255)/10000;


//	  			 if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, TxData0) != HAL_OK)
//	  			  {
//	  				Error_Handler();
//	  			  }
//	  			 while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) != 3) {}
//
//	  			 if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader2, TxData1) != HAL_OK)
//	  			  {
//	  				Error_Handler();
//	  			  }
//	  			 while (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) != 3) {}

	  			 if (try_tx > 2)
	  			 {
	  				 //error_mode |= ERROR_TX_CAN;
	  			 }
	  			switch (setup_mode) {
	  				case 1: if (raw_press>0){
	  							EEPROM_values.max_val = raw_press/100;
	  							EEPROM_values.calib_availible = 0x00;
	  							save_EEPROM(&EEPROM_values);
	  						}
	  						setup_mode = 0;
	  						break;
	  				case 2:	setup_mode = 0;
	  						EEPROM_values.calib_availible = 0xFF;
	  						save_EEPROM(&EEPROM_values);
	  						EEPROM_values.max_val = std_press;
	  						break;
	  				default: 	setup_mode = 0;
	  							break;
	  			}
	  		}
	  	}

	  	//Hier Senderoutine einfügen für NMEA2000

	  	if(((time_el-last_run_nmea)>=nmea_time) && ((time_el-claim_time)>=250))
	  	{
	  		last_run_nmea = time_el;

	  		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, ((float)percent_val)/100, dev_info_par.cap);
	  		NMEA2000_SendTemperature(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, TEMP_SOURCE_NMEA, sensor_data_rx.temp);

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
	  	if(dev_info != 0)
	  	{
	  		dev_info = 0;
	  		NMEA2000_setDevInfo(&hfdcan1, dev_info_par.srcAdr);
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
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 4;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 12;
  hfdcan1.Init.NominalTimeSeg2 = 3;
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


void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t BufferIndexes)
{
	try_tx = 0;
	error_mode &= ~ERROR_TX_CAN;
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

uint8_t check_EEPROM()
{
	if (cfg_data[0] == 0xFF){
		return 0;
	}
	else{
		return 1;
	}
}

void get_EEPROM(calib_data *values)
{
	values->calib_availible = cfg_data[0];
	values->max_val = ((uint32_t)cfg_data[4]<<24)|((uint32_t)cfg_data[3]<<16)|((uint32_t)cfg_data[2]<<8)|(cfg_data[1]);
	values->offset = std_offset;
}

void save_EEPROM(calib_data *values)
{
	/* Nur die Kalibrierbytes im RAM-Cache aendern; config_save() schreibt
	 * den kompletten Block atomar (Ping-Pong, CRC). Bei Fehlschlag bleibt
	 * der alte Datensatz im Flash gueltig - kein Error_Handler noetig. */
	cfg_data[0] = values->calib_availible;
	cfg_data[1] = (uint8_t)(values->max_val & 0xFF);
	cfg_data[2] = (uint8_t)((values->max_val >> 8) & 0xFF);
	cfg_data[3] = (uint8_t)((values->max_val >> 16) & 0xFF);
	cfg_data[4] = (uint8_t)((values->max_val >> 24) & 0xFF);

	config_save();
}


uint8_t check_dac_EEPROM()
{
	return cfg_data[8];
}

void get_dac_EEPROM(dac_calib_data *values)
{
	values->calib_availible = cfg_data[8];
	values->dac_mx = ((uint32_t)cfg_data[12]<<24)|((uint32_t)cfg_data[11]<<16)|((uint32_t)cfg_data[10]<<8)|(cfg_data[9]);
	values->dac_c = ((uint32_t)cfg_data[16]<<24)|((uint32_t)cfg_data[15]<<16)|((uint32_t)cfg_data[14]<<8)|(cfg_data[13]);
}

void set_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values)
{
	cfg_data[17] = nmea_param->fluidType;
	cfg_data[18] = nmea_param->cap;
	memcpy(&cfg_data[19], values->lin_point, 11);
	cfg_data[31] = nmea_param->devInstance;		/* Byte 30 (Adresse) bleibt erhalten */

	config_save();
}

void get_param_eeprom(NMEA_parameter_Device *nmea_param, prod_param *values)
{
	uint8_t lin_std[11] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

	if(cfg_data[17] == 0xFF)
	{
		nmea_param->fluidType = 0x01;
		nmea_param->cap = 100;
		nmea_param->devInstance = 0;
		memcpy(values->lin_point, &lin_std,11);
		set_param_eeprom(nmea_param, values);
	}
	else
	{
		nmea_param->fluidType = cfg_data[17];
		nmea_param->cap = cfg_data[18];
		memcpy(values->lin_point, &cfg_data[19], 11);
		nmea_param->devInstance = (cfg_data[31] <= 0x0F) ? cfg_data[31] : 0;

		if (!lin_table_valid(values->lin_point))	/* defekte Tabelle -> Identitaet */
		{
			memcpy(values->lin_point, &lin_std, 11);
		}
	}
}


/*
 * Verarbeitet eine komplett empfangene Group Function (PGN 126208).
 * Unterstuetzt fuer PGN 127505 (Fluid Level):
 *   Funktionscode 0 (Request): PGN sofort senden
 *   Funktionscode 1 (Command): Feld 1 = Instanz (0..15), Feld 2 = Fluidtyp (0..15),
 *                              Feld 4 = Kapazitaet (uint32, 0,1-L-Schritte)
 * Antwortet mit Acknowledge (Funktionscode 2) inkl. Fehlercodes je Parameter.
 * Hinweis: Feldwerte werden byte-aligned erwartet (wie in gaengigen
 * Open-Source-Stacks), nicht bit-gepackt.
 */
void handle_group_function()
{
	uint8_t fn = gf_buf[0];
	uint32_t target_pgn = gf_buf[1] | ((uint32_t)gf_buf[2] << 8) | ((uint32_t)gf_buf[3] << 16);

	if (target_pgn != 127505)
	{
		if (fn == 1)	/* Kommandos auf fremde PGNs negativ quittieren */
		{
			NMEA2000_SendGFAck(&hfdcan1, dev_info_par.srcAdr, gf_src, target_pgn, 1, NULL, 0);
		}
		return;
	}

	if (fn == 0)		/* Request: einmal sofort senden */
	{
		NMEA2000_SendFluidLevel(&hfdcan1, dev_info_par.srcAdr, dev_info_par.devInstance, dev_info_par.fluidType, ((float)percent_val)/100, dev_info_par.cap);
	}
	else if (fn == 1)	/* Command */
	{
		uint8_t n = gf_buf[5];
		uint8_t pos = 6;
		uint8_t perr[8] = {0};
		uint8_t changed = 0;
		uint8_t inst_changed = 0;

		for (uint8_t i = 0; (i < n) && (i < 8) && (pos < gf_len); i++)
		{
			uint8_t field = gf_buf[pos++];
			switch (field)
			{
			case 1:		/* Instanz (4 bit im PGN) */
				if (gf_buf[pos] <= 0x0F)
				{
					if (dev_info_par.devInstance != gf_buf[pos])
					{
						dev_info_par.devInstance = gf_buf[pos];
						inst_changed = 1;
						changed = 1;
					}
				}
				else
				{
					perr[i] = 3;	/* ausserhalb Bereich */
				}
				pos += 1;
				break;

			case 2:		/* Fluidtyp (4 bit im PGN) */
				if (gf_buf[pos] <= 0x0F)
				{
					dev_info_par.fluidType = gf_buf[pos];
					changed = 1;
				}
				else
				{
					perr[i] = 3;
				}
				pos += 1;
				break;

			case 4:		/* Kapazitaet, uint32 in 0,1 L */
			{
				uint32_t cap01 = gf_buf[pos] | ((uint32_t)gf_buf[pos+1] << 8) | ((uint32_t)gf_buf[pos+2] << 16) | ((uint32_t)gf_buf[pos+3] << 24);
				uint32_t cap_l = cap01 / 10;
				if (cap_l <= 255)	/* EEPROM-Feld ist 1 Byte (Liter) */
				{
					dev_info_par.cap = (uint8_t)cap_l;
					changed = 1;
				}
				else
				{
					perr[i] = 3;
				}
				pos += 4;
				break;
			}

			default:	/* Feld 3 (Fuellstand) ist Messwert, Rest unbekannt */
				perr[i] = 4;	/* Parameter nicht unterstuetzt */
				pos = gf_len;	/* Feldgroesse unbekannt -> Abbruch */
				break;
			}
		}

		if (changed)
		{
			set_param_eeprom(&dev_info_par, &device_param);
		}
		if (inst_changed)
		{
			/* Instanz ist Teil des NAME -> Address Claim wiederholen */
			NMEA2000_AdrClaim(&hfdcan1, dev_info_par.srcAdr, dev_info_par.UniqueNumber, dev_info_par.MFRcode, dev_info_par.DeviceFunction, dev_info_par.DeviceClass, dev_info_par.devInstance, dev_info_par.sysInstance, dev_info_par.indGroup);
			claim_time = HAL_GetTick();
		}

		NMEA2000_SendGFAck(&hfdcan1, dev_info_par.srcAdr, gf_src, target_pgn, 0, perr, n);
	}
}


/*
 * Prueft die Stuetzstellen-Tabelle: 11 Werte in Prozent (0..100),
 * monoton nicht fallend. lin_point[i] = Volumen-% bei Fuellhoehe i*10 %.
 */
uint8_t lin_table_valid(uint8_t *t)
{
	for (uint8_t i = 0; i < 11; i++)
	{
		if (t[i] > 100)
		{
			return 0;
		}
	}
	for (uint8_t i = 0; i < 10; i++)
	{
		if (t[i+1] < t[i])
		{
			return 0;
		}
	}
	return 1;
}

/*
 * Linearisierung fuer unregelmaessige Tankformen:
 * bildet die Fuellhoehe (0..10000 = 0..100,00 %) ueber die 11 Stuetzstellen
 * per stueckweiser linearer Interpolation aufs Volumen ab.
 * Standardtabelle 0,10,..,100 = Identitaet (gleichmaessiger Tank).
 */
uint16_t linearize_percent(uint16_t raw)
{
	if (raw >= 10000)
	{
		return (uint16_t)device_param.lin_point[10] * 100;
	}

	uint8_t idx = raw / 1000;			/* Segment 0..9 */
	uint16_t seg_off = raw % 1000;		/* Position im Segment */
	int32_t y0 = (int32_t)device_param.lin_point[idx] * 100;
	int32_t y1 = (int32_t)device_param.lin_point[idx + 1] * 100;

	return (uint16_t)(y0 + ((y1 - y0) * (int32_t)seg_off) / 1000);
}

/*
 * Verarbeitet proprietaere Konfiguration (PGN 126720):
 *   [Header 0xFE 0x9F] [0x01] [11 Stuetzstellen]  -> Tabelle schreiben, Antwort 0x81 + Status
 *   [Header 0xFE 0x9F] [0x02]                     -> Tabelle lesen,     Antwort 0x82 + 11 Werte
 */
void handle_prop_config()
{
	uint8_t reply[16];

	if ((gf_len < 3) || (gf_buf[0] != PROP_HDR_0) || (gf_buf[1] != PROP_HDR_1))
	{
		return;		/* nicht unser Herstellercode */
	}

	reply[0] = PROP_HDR_0;
	reply[1] = PROP_HDR_1;

	if ((gf_buf[2] == PROP_CMD_SET_LIN) && (gf_len >= 14))
	{
		uint8_t ok = lin_table_valid(&gf_buf[3]);
		if (ok)
		{
			memcpy(device_param.lin_point, &gf_buf[3], 11);
			set_param_eeprom(&dev_info_par, &device_param);
		}
		reply[2] = 0x81;
		reply[3] = ok ? 0 : 3;	/* 0 = OK, 3 = ungueltig (>100 oder nicht monoton) */
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
	else if (gf_buf[2] == PROP_CMD_GET_LIN)
	{
		reply[2] = 0x82;
		memcpy(&reply[3], device_param.lin_point, 11);
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 14);
	}
	else if (gf_buf[2] == PROP_CMD_CALIB)
	{
		/* Wie der Taster-Kalibriermodus: aktuellen Druck als 100 % setzen.
		 * Nur bei positivem Druck (Sensor plausibel angeschlossen). */
		uint8_t ok = 0;
		if (raw_press > 0)
		{
			EEPROM_values.max_val = raw_press / 100;
			EEPROM_values.calib_availible = 0x00;
			save_EEPROM(&EEPROM_values);
			ok = 1;
		}
		reply[2] = 0x83;
		reply[3] = ok ? 0 : 1;	/* 1 = kein gueltiger Druck */
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
	else if (gf_buf[2] == PROP_CMD_RESET)
	{
		/* Kalibrierung verwerfen -> Werkswert (std_press) beim naechsten Boot */
		EEPROM_values.calib_availible = 0xFF;
		save_EEPROM(&EEPROM_values);
		EEPROM_values.max_val = std_press;
		reply[2] = 0x84;
		reply[3] = 0;
		NMEA2000_SendProprietaryFP(&hfdcan1, dev_info_par.srcAdr, gf_src, reply, 4);
	}
}


/* Config-Byte 30: zuletzt erfolgreich geclaimte NMEA2000-Quelladresse */
uint8_t get_adr_eeprom()
{
	uint8_t adr = cfg_data[30];

	if (adr > 251)		/* 0xFF = leer, >251 = ungueltig */
	{
		adr = 0x21;		/* Standardadresse */
	}
	return adr;
}

void set_adr_eeprom(uint8_t adr)
{
	if (cfg_data[30] == adr)
	{
		return;			/* nur bei Aenderung schreiben -> minimaler Flash-Verschleiss */
	}

	cfg_data[30] = adr;
	config_save();
}


void set_led(int32_t red, uint32_t green, int32_t blue, int32_t brightness)
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

void set_volt(uint16_t percent, dac_calib_data * datas)
{
	uint32_t volt = 0;
	uint16_t dac_val = 0;

	volt = (4 * percent)/10 + 500;

	//dac_val = (volt * 12409) / 10000;														// Hier müssen noch Kalibirierparameter eingefügt werden, dass die Spannung genau bleibt
	dac_val = ((volt * datas->dac_mx )+datas->dac_c)/10000;

	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
}

void set_volt_raw(uint16_t volt, dac_calib_data * datas)
{
	//uint16_t dac_val = ((volt * datas->dac_mx )+datas->dac_c)/10000;

	HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, volt);
}

uint16_t calc_percent(calib_data *datas, int64_t mw)
{

	if(mw > datas->max_val*100)
	{
		mw = datas->max_val*100;
	}
	else if(mw < 0)
	{
		mw = 0;
	}
	uint32_t res_val;
	mw = mw*100;
	res_val = mw / datas->max_val;
	return (uint16_t)res_val;
}
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

sensor_mess get_value()
{
	static sensor_mess last_good = {0, 0};	/* letzter gueltiger Messwert */
	sensor_mess mess_data;
	uint8_t rxBuffer[5] = {0};
	uint8_t start_Reg = 0x06;
	float k = 12.8;		//25.6 -> 20kpa 12.8 40kpa
	uint8_t tx_arr[2];
	tx_arr[0] = 0x30;
	tx_arr[1] = 0x0A;
	double raw_pressure;
	float raw_temp;
	uint8_t i2c_ok = 1;

	/* Fix: Timeout 25 ms statt 2500 ms - blockiert die Hauptschleife
	 * bei Sensorausfall nicht mehr sekundenlang. */
	if(HAL_I2C_Master_Transmit(&hi2c1, 0x6D<<1, &start_Reg, 1, 25)!= HAL_OK)
	{
		error_mode |= ERROR_I2C;
		i2c_ok = 0;
	}

	if(i2c_ok && (HAL_I2C_Master_Receive(&hi2c1, 0x6D<<1, rxBuffer, 5, 25) != HAL_OK))
	{
		error_mode |= ERROR_I2C;
		i2c_ok = 0;
	}

	/* naechste Messung anstossen (auch nach Fehler versuchen) */
	HAL_I2C_Master_Transmit(&hi2c1, 0x6D<<1, tx_arr, 2, 25);

	if(i2c_ok == 0)
	{
		/* Fix: bei I2C-Fehler keinen uninitialisierten Puffer auswerten,
		 * sondern letzten gueltigen Wert zurueckgeben. */
		return last_good;
	}

	if(rxBuffer[0]&0x80)
	{
		raw_pressure = ((double)((rxBuffer[0]<<16) | (rxBuffer[1]<<8) | (rxBuffer[2]))-16777216)/k;
	}
	else
	{
		raw_pressure = (double)((rxBuffer[0]<<16) | (rxBuffer[1]<<8) | (rxBuffer[2]))/k;
	}


	if(rxBuffer[3]&0x80)
	{
		raw_temp = ((float)((rxBuffer[3]<<8) | (rxBuffer[4]))-65536)/256;
	}
	else
	{
		raw_temp = (float)((rxBuffer[3]<<8) | (rxBuffer[4]))/256;
	}

	mess_data.pressure = raw_pressure-EEPROM_values.offset;
	mess_data.temp = (int16_t)(raw_temp * 100);	/* jetzt in 0,01 Grad C (fuer PGN 130312) */

	last_good = mess_data;

	return mess_data;
}

void init_Sensor()
{
	uint8_t tx_arr[2];
	tx_arr[0] = 0x30;
	tx_arr[1] = 0x0A;
	/* Fix: vorher 'while(cmd_reg % 0x08)' mit cmd_reg=0 -> Schleife lief nie.
	 * Jetzt: warten bis Busy-Bit (0x08 in Reg 0x30) geloescht ist,
	 * mit Versuchslimit statt Endlosschleife bei fehlendem Sensor. */
	uint8_t cmd_reg = 0x08;
	uint8_t retries = 0;

	HAL_I2C_Master_Transmit(&hi2c1, 0x6D<<1, tx_arr, 2, 25);

	while ((cmd_reg & 0x08) && (retries++ < 100))
	{
		HAL_I2C_Master_Transmit(&hi2c1, 0x6D<<1, tx_arr, 1, 25);

		HAL_I2C_Master_Receive(&hi2c1, 0x6D<<1, &cmd_reg, 1, 25);
	}

}

/*
 * Sendet den aktuellen Sensorzustand als maschinenlesbare Zeile an die App.
 * Format:  STAT;L=<%>;T=<C>;F=<typ>;C=<L>;I=<inst>;CAL=<0/1>\n
 * L: Füllstand in %, T: Temperatur in Grad C, F: Fluidtyp (0..15),
 * C: Kapazität (Liter), I: Instanz, CAL: 1 = kalibriert.
 */
void ble_send_status()
{
	char line[80];

	/* percent_val: 100,00 % = 10000 */
	int p_int = percent_val / 100;
	int p_frac = (percent_val % 100) / 10;

	/* temp: 0,01 Grad C, Vorzeichen sauber behandeln */
	int16_t t = sensor_data_rx.temp;
	const char *tsign = (t < 0) ? "-" : "";
	int ta = (t < 0) ? -t : t;

	int cal = (EEPROM_values.calib_availible == 0x00) ? 1 : 0;

	snprintf(line, sizeof(line), "STAT;L=%d.%d;T=%s%d.%02d;F=%d;C=%d;I=%d;CAL=%d\n",
			 p_int, p_frac, tsign, ta / 100, ta % 100,
			 dev_info_par.fluidType, dev_info_par.cap, dev_info_par.devInstance, cal);

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
 * Verarbeitet ein Textkommando von der App (CMD_DATA_IND).
 * Unterstützt (Groß-/Kleinschreibung egal):
 *   GET            aktuellen Status sofort senden
 *   CAL100         aktuellen Druck als 100 % kalibrieren
 *   CALRESET       Kalibrierung auf Werkswert zurücksetzen
 *   FLUID <0..15>  Fluidtyp setzen
 *   CAP <1..255>   Tankkapazität (Liter) setzen
 *   INST <0..15>   Instanz setzen
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

	if (strncasecmp(cmd, "GET", 3) == 0)
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
		if (raw_press > 0)
		{
			EEPROM_values.max_val = raw_press / 100;
			EEPROM_values.calib_availible = 0x00;
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
		EEPROM_values.calib_availible = 0xFF;
		save_EEPROM(&EEPROM_values);
		EEPROM_values.max_val = std_press;
		BLE_SendString("OK CALRESET\n");
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
	else if (strncasecmp(cmd, "NAME ", 5) == 0)
	{
		const char *name = cmd + 5;
		if (*name != '\0')
		{
			/* erst bestätigen, dann Modul umbenennen (Modul startet danach neu
			 * und trennt die Verbindung). */
			BLE_SendString("OK NAME\n");
			HAL_Delay(50);
			BLE_SetDeviceName(name);
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
