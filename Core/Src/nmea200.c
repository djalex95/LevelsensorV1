/*
 * nmea200.c
 *
 *  Created on: May 22, 2024
 *      Author: a_hae
 */

#include "nmea200.h"
#include "main.h"		/* fuer Error_Handler() */
#include "version.h"	/* zentrale Firmware-Version */

typedef union nmea_int16_convert{
	uint8_t small_arr[2];
	uint16_t max_val;
}nmea_int16_convert;

typedef union nmea_int32_convert{
	uint8_t small_arr[4];
	uint32_t max_val;
}nmea_int32_convert;

typedef struct prod_param{
	uint8_t fluid_type;
	uint8_t tank_cap;
	uint8_t lin_point[11];
}prod_param;



extern volatile uint8_t adr_claim;
extern volatile uint8_t adr_lost;
extern volatile uint8_t prod_info;
extern volatile uint8_t dev_info;
extern NMEA_parameter_Product p_info;
extern NMEA_parameter_Device dev_info_par;


FDCAN_RxHeaderTypeDef RxHeader,RxHeader0;
uint8_t RxData[8];
uint8_t RxData0[8];

/* --- Fast-Packet-Reassembly fuer PGN 126208 (Group Function) --- */
uint8_t gf_buf[GF_BUF_SIZE];
volatile uint8_t gf_ready = 0;
uint8_t gf_src = 0;
uint8_t gf_len = 0;
uint32_t gf_pgn = 0;
static uint8_t gf_frame_next = 0xFF;	/* 0xFF = keine Reassembly aktiv */
static uint8_t gf_pos = 0;
static uint8_t gf_total = 0;
static uint32_t gf_rx_pgn = 0;

/* Verarbeitet einen Fast-Packet-Frame (PGN 126208/126720, ISR-Kontext, leichtgewichtig).
 * Frame 0: [SeqZaehler, Gesamtlaenge, 6 Datenbytes], Folgeframes: [Zaehler, 7 Datenbytes] */
static void gf_feed(uint32_t pgn, uint8_t src, uint8_t *frame)
{
	uint8_t counter = frame[0] & 0x1F;

	if (gf_ready)		/* Hauptschleife hat den letzten Befehl noch nicht abgeholt */
	{
		return;
	}

	if (counter == 0)
	{
		gf_total = frame[1];
		if (gf_total <= GF_BUF_SIZE)
		{
			memcpy(gf_buf, &frame[2], 6);
			gf_pos = 6;
			gf_frame_next = 1;
			gf_src = src;
			gf_rx_pgn = pgn;
		}
		else
		{
			gf_frame_next = 0xFF;	/* zu lang -> verwerfen */
		}
	}
	else if ((counter == gf_frame_next) && (gf_pos < GF_BUF_SIZE))
	{
		uint8_t chunk = 7;
		if ((uint8_t)(gf_pos + chunk) > GF_BUF_SIZE)
		{
			chunk = GF_BUF_SIZE - gf_pos;
		}
		memcpy(&gf_buf[gf_pos], &frame[1], chunk);
		gf_pos += chunk;
		gf_frame_next++;
	}
	else
	{
		gf_frame_next = 0xFF;		/* Sequenzfehler -> verwerfen */
	}

	if ((gf_frame_next != 0xFF) && (gf_pos >= gf_total))
	{
		gf_len = gf_total;
		gf_pgn = gf_rx_pgn;
		gf_ready = 1;
		gf_frame_next = 0xFF;
	}
}

/* Extrahiert die PGN aus einer 29-bit-CAN-ID (inkl. EDP/DP-Bits).
 * Bei PDU1 (PF < 240) ist das PS-Byte die Zieladresse und gehoert nicht zur PGN. */
static uint32_t canid_to_pgn(uint32_t id)
{
	uint32_t pf = (id >> 16) & 0xFF;
	if (pf < 240)
	{
		return (id >> 8) & 0x3FF00;
	}
	return (id >> 8) & 0x3FFFF;
}

/* Baut den eigenen 64-bit-NAME exakt so auf, wie ihn NMEA2000_AdrClaim sendet
 * (little endian). Wird fuer den Arbitrierungs-Vergleich benoetigt. */
static uint64_t build_own_name(void)
{
	uint64_t name;
	name  = (uint64_t)((dev_info_par.UniqueNumber & 0x1FFFFF) | (((uint32_t)(dev_info_par.MFRcode & 0x7FF)) << 21));
	name |= (uint64_t)dev_info_par.devInstance << 32;
	name |= (uint64_t)dev_info_par.DeviceFunction << 40;
	name |= (uint64_t)((dev_info_par.DeviceClass & 0x7F) << 1) << 48;
	name |= (uint64_t)(0x80 | ((dev_info_par.indGroup & 0x7) << 4) | (dev_info_par.sysInstance & 0x0F)) << 56;
	return name;
}

/* Wartet mit Timeout, bis die TX-FIFO leer ist (3 freie Plaetze).
 * Verhindert Endlos-Haenger bei Bus-Off / fehlendem ACK. */
static uint8_t wait_tx_fifo_free(FDCAN_HandleTypeDef *can_handle)
{
	uint32_t start = HAL_GetTick();
	while (HAL_FDCAN_GetTxFifoFreeLevel(can_handle) != 3)
	{
		if ((HAL_GetTick() - start) > 10)
		{
			return 0;
		}
	}
	return 1;
}

uint8_t init_p_struct(NMEA_parameter_Product *p_info_struct)
{
	uint8_t filled_string[32] = {[0 ... 31] = 0xFF};
	memcpy(p_info_struct->ModelID, filled_string,32);
	strcpy(p_info_struct->ModelID, "Simple Level Monitor");
	memcpy(p_info_struct->ModelSerialCode, filled_string,32);
	strcpy(p_info_struct->ModelSerialCode, "00000002");
	memcpy(p_info_struct->ModelVersion, filled_string,32);
	strcpy(p_info_struct->ModelVersion, HW_VERSION_STR);
	memcpy(p_info_struct->SwCode, filled_string,32);
	strcpy(p_info_struct->SwCode, FW_VERSION_STR);

	p_info_struct->ProductCode=102;

	return 1;
}

uint8_t NMEA2000_setPInfo(FDCAN_HandleTypeDef *can_handle, NMEA_parameter_Product *p_parameter, uint8_t src_adr)
{
	uint8_t can_msg[140] = {[0 ... 139] = 0xFF};
	uint8_t tx_msg[8];

	uint32_t PGN = 126996;
	uint8_t Priority = 6;


	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	can_msg[0] = 0x86;
	can_msg[1] = 0x35;		//nmea Version

	//uint16_convert.max_val = p_parameter.ProductCode;
	//can_msg[2] = uint16_convert.small_arr[0];
	//can_msg[3] = uint16_convert.small_arr[1];

	can_msg[2] = 0x08;
	can_msg[3] = (uint8_t)p_parameter->ProductCode;

	can_msg[4] = 0x00;

	memcpy(can_msg+5, p_parameter->ModelID,32);
	memcpy(can_msg+37, p_parameter->SwCode,32);
	memcpy(can_msg+69, p_parameter->ModelVersion,32);
	memcpy(can_msg+101, p_parameter->ModelSerialCode,32);
	can_msg[133] = 0x30;
	can_msg[134] = 0x30;
	can_msg[135] = 0x30;
	can_msg[136] = 0x31;



	for (uint8_t var = 0; var < 20; ++var) {
		tx_msg[0] = var;
		memcpy(tx_msg + 1, can_msg + (var*7), 7);

		if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_msg) != HAL_OK)
		{
			return 0;
		}
		if (wait_tx_fifo_free(can_handle) == 0)
		{
			return 0;
		}
	}
	return 1;
}
uint8_t NMEA2000_setDevInfo(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr)
{

	uint8_t can_msg[70] = {[0 ... 69] = 0xFF};
	uint8_t tx_msg[8];

	uint32_t PGN = 126998;
	uint8_t Priority = 6;


	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	strcpy(can_msg, "DIY NMEA2000 Lib");

	for (uint8_t var = 0; var < 10; ++var) {
		tx_msg[0] = var;
		memcpy(tx_msg + 1, can_msg + (var*7), 7);

		if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_msg) != HAL_OK)
		{
			return 0;
		}
		if (wait_tx_fifo_free(can_handle) == 0)
		{
			return 0;
		}
	}

	return 1;
}

uint8_t NMEA2000_config(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr)
{
	FDCAN_FilterTypeDef sFilterConfig;


	  /* Configure reception filter to Rx FIFO 0 */
	    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
	    sFilterConfig.FilterIndex = 0;
	    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
	    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	    sFilterConfig.FilterID1 = (src_adr << 8);
	    sFilterConfig.FilterID2 = 0xFF00;
	    if (HAL_FDCAN_ConfigFilter(can_handle, &sFilterConfig) != HAL_OK)
	    {
	      return 0;
	    }
	    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
		sFilterConfig.FilterIndex = 1;
		sFilterConfig.FilterType = FDCAN_FILTER_MASK;
		sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
		sFilterConfig.FilterID1 = 0xFF00;
		sFilterConfig.FilterID2 = 0xFF00;
		if (HAL_FDCAN_ConfigFilter(can_handle, &sFilterConfig) != HAL_OK)
		{
			return 0;
		}

		 if (HAL_FDCAN_ActivateNotification(can_handle, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
			   {
			 return 0;
			   }
		 if (HAL_FDCAN_ActivateNotification(can_handle, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0) != HAL_OK)
			{
			 return 0;
			}
		 return 1;
}


uint8_t NMEA2000_AdrClaim(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, unsigned long UniqueNumber, int ManufacturerCode,
        unsigned char DeviceFunction, unsigned char DeviceClass,
        unsigned char DeviceInstance, unsigned char SystemInstance, unsigned char IndustryGroup
        )
{
	uint8_t tx_data[8];
	nmea_int32_convert convert_m;
	uint32_t PGN = 60928;
	uint8_t Priority = 6;

	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	convert_m.max_val = ((UniqueNumber&0x1FFFFF) | ((unsigned long)(ManufacturerCode&0x7ff))<<21);
	tx_data[0]=convert_m.small_arr[0];
	tx_data[1]=convert_m.small_arr[1];
	tx_data[2]=convert_m.small_arr[2];
	tx_data[3]=convert_m.small_arr[3];
	tx_data[4]=(uint8_t) DeviceInstance;
	tx_data[5]=(uint8_t) DeviceFunction;
	tx_data[6]=(uint8_t) ((DeviceClass&0x7f)<<1);
	tx_data[7]=(uint8_t) ( 0x80 | ((IndustryGroup&0x7)<<4) | (SystemInstance&0x0f) );

	if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_data) != HAL_OK)
	{
		return 0;
	}
	if (wait_tx_fifo_free(can_handle) == 0)
	{
		return 0;
	}

	return 1;
}

uint8_t NMEA2000_SendFluidLevel(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t Instance, uint8_t FluidType, float Level, uint8_t Capacity)
{
	uint8_t tx_data[8];
	uint32_t PGN = 127505;
	uint8_t Priority = 6;
	double vd = 0;
	int16_t vi = 0;
	int32_t vii = 0;
	nmea_int16_convert convert_n;
	nmea_int32_convert convert_m;

	FDCAN_TxHeaderTypeDef TxHeader;




	//Generierung der ID



	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	//Generierung der Daten zum Senden

	tx_data[0]=((Instance&0x0f) | ((FluidType&0x0f)<<4));
	//Level berechnen
	//vd=round((Level/0.004));
	vd=round((Level*250));
	vi = (vd>=N2kInt16Min && vd<N2kInt16OR)?(int16_t)vd:N2kInt16OR;
	convert_n.max_val = vi;
	tx_data[1]=convert_n.small_arr[0];
	tx_data[2]=convert_n.small_arr[1];

	//Capacity berechnen
	vd=round((Capacity/0.1));
	vii = (vd>=0 && vd<N2kUInt32OR)?(uint32_t)vd:N2kUInt32OR;
	convert_m.max_val = vii;
	tx_data[3]=convert_m.small_arr[0];
	tx_data[4]=convert_m.small_arr[1];
	tx_data[5]=convert_m.small_arr[2];
	tx_data[6]=convert_m.small_arr[3];
	tx_data[7]= 0xFF;

	if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_data) != HAL_OK)
	{
		return 0;
	}
	if (wait_tx_fifo_free(can_handle) == 0)
	{
		return 0;
	}

	return 1;

}

/*
 * Sendet PGN 130312 (Temperature).
 * temp_centi_deg: Temperatur in 0,01 Grad C (z.B. 2345 = 23,45 Grad C)
 * source: NMEA2000 Temperature Source (z.B. 2 = Inside Temperature)
 */
uint8_t NMEA2000_SendTemperature(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t Instance, uint8_t Source, int16_t temp_centi_deg)
{
	uint8_t tx_data[8];
	uint32_t PGN = 130312;
	uint8_t Priority = 6;
	uint32_t temp_k100;

	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	// 0,01 Grad C -> 0,01 Kelvin (uint16, little endian)
	temp_k100 = (uint32_t)((int32_t)temp_centi_deg + 27315);

	tx_data[0] = 0xFF;						// SID (nicht verwendet)
	tx_data[1] = Instance;
	tx_data[2] = Source;
	tx_data[3] = (uint8_t)(temp_k100 & 0xFF);
	tx_data[4] = (uint8_t)((temp_k100 >> 8) & 0xFF);
	tx_data[5] = 0xFF;						// Set Temperature: n/a
	tx_data[6] = 0xFF;
	tx_data[7] = 0xFF;						// reserved

	if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_data) != HAL_OK)
	{
		return 0;
	}
	if (wait_tx_fifo_free(can_handle) == 0)
	{
		return 0;
	}

	return 1;
}

uint8_t NMEA2000_SendLabel(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr)
{
	uint8_t can_msg[70] = {[0 ... 69] = 0xFF};
	uint8_t tx_msg[8];

	uint32_t PGN = 130060;
	uint8_t Priority = 6;


	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, 0xFF);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	strcpy(can_msg, "TestSens");

	for (uint8_t var = 0; var < 5; ++var) {
		tx_msg[0] = var;
		memcpy(tx_msg + 1, can_msg + (var*7), 7);

		if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_msg) != HAL_OK)
		{
			return 0;
		}
		if (wait_tx_fifo_free(can_handle) == 0)
		{
			return 0;
		}
	}

	return 1;
}

/*
 * Sendet eine Acknowledge Group Function (PGN 126208, Funktionscode 2) an 'dest'.
 * pgn_err: 0 = OK, 1 = PGN nicht unterstuetzt, ...
 * param_errs: 4-bit-Fehlercode je Parameter (0 = OK, 3 = ausserhalb Bereich,
 *             4 = Parameter nicht unterstuetzt), je zwei in ein Byte gepackt.
 */
uint8_t NMEA2000_SendGFAck(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t dest, uint32_t target_pgn, uint8_t pgn_err, uint8_t *param_errs, uint8_t n_params)
{
	uint8_t payload[16];
	uint8_t tx_msg[8];
	uint8_t len;
	uint32_t PGN = 126208;
	uint8_t Priority = 6;

	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, dest);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	memset(payload, 0xFF, sizeof(payload));
	payload[0] = 2;								/* Acknowledge */
	payload[1] = (uint8_t)(target_pgn & 0xFF);
	payload[2] = (uint8_t)((target_pgn >> 8) & 0xFF);
	payload[3] = (uint8_t)((target_pgn >> 16) & 0xFF);
	payload[4] = (uint8_t)(pgn_err & 0x0F);		/* High-Nibble: Intervall-Fehlercode = 0 */
	payload[5] = n_params;

	for (uint8_t i = 0; i < n_params && i < 8; i++)
	{
		uint8_t err = (param_errs != NULL) ? (param_errs[i] & 0x0F) : 0;
		if ((i & 1) == 0)
		{
			payload[6 + i/2] = err;				/* Low-Nibble zuerst */
		}
		else
		{
			payload[6 + i/2] |= (uint8_t)(err << 4);
		}
	}
	len = 6 + (n_params + 1) / 2;

	/* Fast-Packet-Rahmen senden: Frame 0 = [0, len, 6 Bytes], danach [n, 7 Bytes] */
	uint8_t nframes = 1;
	if (len > 6)
	{
		nframes += (uint8_t)((len - 6 + 6) / 7);
	}

	for (uint8_t var = 0; var < nframes; ++var) {
		memset(tx_msg, 0xFF, sizeof(tx_msg));
		tx_msg[0] = var;
		if (var == 0)
		{
			tx_msg[1] = len;
			memcpy(tx_msg + 2, payload, 6);
		}
		else
		{
			uint8_t off = (uint8_t)(6 + (var - 1) * 7);
			uint8_t chunk = 7;
			if ((uint8_t)(off + chunk) > len)
			{
				chunk = len - off;
			}
			memcpy(tx_msg + 1, payload + off, chunk);
		}

		if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_msg) != HAL_OK)
		{
			return 0;
		}
		if (wait_tx_fifo_free(can_handle) == 0)
		{
			return 0;
		}
	}
	return 1;
}

/*
 * Sendet eine proprietaere Fast-Packet-Nachricht (PGN 126720) an 'dest'.
 * Der Aufrufer liefert den kompletten Payload inkl. Proprietary-Header.
 */
uint8_t NMEA2000_SendProprietaryFP(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t dest, uint8_t *payload, uint8_t len)
{
	uint8_t tx_msg[8];
	uint32_t PGN = 126720;
	uint8_t Priority = 6;

	FDCAN_TxHeaderTypeDef TxHeader;

	TxHeader.Identifier = (uint32_t)N2ktoCanID( (unsigned char)Priority, (unsigned long) PGN, (unsigned long)src_adr, dest);
	TxHeader.IdType = FDCAN_EXTENDED_ID;
	TxHeader.TxFrameType = FDCAN_DATA_FRAME;
	TxHeader.DataLength = FDCAN_DLC_BYTES_8;
	TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
	TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
	TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	TxHeader.MessageMarker = 0;

	uint8_t nframes = 1;
	if (len > 6)
	{
		nframes += (uint8_t)((len - 6 + 6) / 7);
	}

	for (uint8_t var = 0; var < nframes; ++var) {
		memset(tx_msg, 0xFF, sizeof(tx_msg));
		tx_msg[0] = var;
		if (var == 0)
		{
			tx_msg[1] = len;
			uint8_t chunk0 = (len < 6) ? len : 6;
			memcpy(tx_msg + 2, payload, chunk0);
		}
		else
		{
			uint8_t off = (uint8_t)(6 + (var - 1) * 7);
			uint8_t chunk = 7;
			if ((uint8_t)(off + chunk) > len)
			{
				chunk = len - off;
			}
			memcpy(tx_msg + 1, payload + off, chunk);
		}

		if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &TxHeader, tx_msg) != HAL_OK)
		{
			return 0;
		}
		if (wait_tx_fifo_free(can_handle) == 0)
		{
			return 0;
		}
	}
	return 1;
}

/* Wechselt die eigene Quelladresse: FDCAN stoppen, RX-Filter auf die neue
 * Adresse umkonfigurieren, wieder starten. */
uint8_t NMEA2000_change_address(FDCAN_HandleTypeDef *can_handle, uint8_t new_adr)
{
	if (HAL_FDCAN_Stop(can_handle) != HAL_OK)
	{
		return 0;
	}
	if (NMEA2000_config(can_handle, new_adr) == 0)
	{
		return 0;
	}
	if (HAL_FDCAN_Start(can_handle) != HAL_OK)
	{
		return 0;
	}
	return 1;
}

unsigned long N2ktoCanID(unsigned char priority, unsigned long PGN, unsigned long Source, unsigned char Destination)
{
  unsigned char CanIdPF = (unsigned char) (PGN >> 8);

  if (CanIdPF < 240) {  // PDU1 format
     if ( (PGN & 0xff) != 0 ) return 0;  // for PDU1 format PGN lowest byte has to be 0 for the destination.
     return ( ((unsigned long)(priority & 0x7))<<26 | PGN<<8 | ((unsigned long)Destination)<<8 | (unsigned long)Source);
  } else { // PDU2 format
     return ( ((unsigned long)(priority & 0x7))<<26 | PGN<<8 | (unsigned long)Source);
  }
}


void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{

	const uint8_t req_pInfo[] = {0x14, 0xF0, 0x01};
	const uint8_t req_dInfo[] = {0x16, 0xF0, 0x01};
	const uint8_t adr_req[]   = {0x00, 0xEE, 0x00};

		if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0)
		  {
			/* Retrieve Rx messages from RX FIFO0 (Frames an die eigene Adresse) */
			if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader0, RxData0) != HAL_OK)
			{
			  Error_Handler();
			}

			else
			{
				uint32_t pgn = canid_to_pgn(RxHeader0.Identifier);

				if(pgn == 59904)		/* ISO Request */
				{
					if(memcmp(RxData0, req_pInfo, 3) == 0)
					{
						prod_info++;
					}

					if(memcmp(RxData0, req_dInfo, 3) == 0)
					{
						dev_info++;
					}

					if(memcmp(RxData0, adr_req, 3) == 0)	/* gezielter Request fuer Address Claim */
					{
						adr_claim++;
					}
				}
				else if((pgn == 126208) || (pgn == 126720))	/* Group Function / Proprietary (Fast Packet) */
				{
					gf_feed(pgn, (uint8_t)(RxHeader0.Identifier & 0xFF), RxData0);
				}

			}
		  }

}

void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo1ITs)
{
	const uint8_t adr_req[] = {0x00, 0xEE, 0x00};

	if((RxFifo1ITs & FDCAN_IT_RX_FIFO1_NEW_MESSAGE) != 0)
	  {
		/* Retrieve Rx messages from RX FIFO1 (Broadcast-Frames) */
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO1, &RxHeader, RxData) != HAL_OK)
		{
		  Error_Handler();
		}

		else
		{
			uint32_t pgn = canid_to_pgn(RxHeader.Identifier);
			uint8_t rx_src = (uint8_t)(RxHeader.Identifier & 0xFF);

			if((pgn == 60928) && (rx_src == dev_info_par.srcAdr))
			{
				/* Adresskonflikt: fremder Claim auf unserer Adresse.
				 * NAME-Vergleich nach ISO 11783-5: der NIEDRIGERE NAME gewinnt. */
				uint64_t rx_name = 0;
				for (int i = 7; i >= 0; i--)
				{
					rx_name = (rx_name << 8) | RxData[i];
				}

				if (build_own_name() < rx_name)
				{
					adr_claim++;	/* gewonnen: eigenen Claim wiederholen */
				}
				else
				{
					adr_lost++;		/* verloren: Hauptschleife weicht auf neue Adresse aus */
				}
			}
			else if((pgn == 59904) && (memcmp(RxData, adr_req, 3) == 0))
			{
				adr_claim++;		/* Broadcast-Request fuer Address Claim */
			}

		}
	  }
}
