/*
 * nmea200.h
 *
 *  Created on: May 22, 2024
 *      Author: a_hae
 */

#ifndef INC_NMEA200_H_
#define INC_NMEA200_H_

#include "stm32g0xx_hal.h"
#include <math.h>
#include <string.h>

#define N2kInt8OR 0x7e
#define N2kUInt8OR 0xfe
#define N2kInt16OR 0x7ffe
#define N2kUInt16OR 0xfffe
#define N2kInt32OR 0x7ffffffe
#define N2kUInt32OR 0xfffffffe

#define N2kInt32Min -2147483648L
#define N2kInt24OR  8388606L
#define N2kInt24Min -8388608L
#define N2kInt16Min -32768
#define N2kInt8Min  -128

typedef struct
{
	uint16_t ProductCode;
	uint8_t ModelID[32];
	uint8_t SwCode[32];
	uint8_t ModelVersion[32];
	uint8_t ModelSerialCode[32];

}NMEA_parameter_Product;

typedef struct
{
	uint32_t UniqueNumber;
	uint8_t DeviceFunction;
	uint8_t DeviceClass;
	uint16_t MFRcode;
	uint8_t devInstance;
	uint8_t sysInstance;
	uint8_t indGroup;
	uint8_t srcAdr;
	uint8_t fluidType;
	uint8_t cap;

}NMEA_parameter_Device;



uint8_t init_p_struct(NMEA_parameter_Product *p_info_struct);

uint8_t NMEA2000_setPInfo(FDCAN_HandleTypeDef *can_handle, NMEA_parameter_Product *p_parameter, uint8_t src_adr);
uint8_t NMEA2000_setDevInfo(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr);
uint8_t NMEA2000_AdrClaim(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, unsigned long UniqueNumber, int ManufacturerCode,
        unsigned char DeviceFunction, unsigned char DeviceClass,
        unsigned char DeviceInstance, unsigned char SystemInstance, unsigned char IndustryGroup
        );
uint8_t NMEA2000_config(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr);
uint8_t NMEA2000_change_address(FDCAN_HandleTypeDef *can_handle, uint8_t new_adr);
uint8_t NMEA2000_SendFluidLevel(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t Instance, uint8_t FluidType, float Level, uint8_t Capacity);
uint8_t NMEA2000_SendTemperature(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t Instance, uint8_t Source, int16_t temp_centi_deg);
uint8_t NMEA2000_SendLabel(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr);
uint8_t NMEA2000_SendGFAck(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t dest, uint32_t target_pgn, uint8_t pgn_err, uint8_t *param_errs, uint8_t n_params);

/* Empfangspuffer fuer PGN 126208 (Group Function, Fast Packet).
 * Wird im RX-Interrupt gefuellt; gf_ready signalisiert der Hauptschleife
 * eine vollstaendig empfangene Group Function. */
#define GF_BUF_SIZE 24
extern uint8_t gf_buf[GF_BUF_SIZE];
extern volatile uint8_t gf_ready;
extern uint8_t gf_src;
extern uint8_t gf_len;
extern uint32_t gf_pgn;		/* PGN der empfangenen Fast-Packet-Nachricht (126208 oder 126720) */

uint8_t NMEA2000_SendProprietaryFP(FDCAN_HandleTypeDef *can_handle, uint8_t src_adr, uint8_t dest, uint8_t *payload, uint8_t len);

unsigned long N2ktoCanID(unsigned char priority, unsigned long PGN, unsigned long Source, unsigned char Destination);



#endif /* INC_NMEA200_H_ */
