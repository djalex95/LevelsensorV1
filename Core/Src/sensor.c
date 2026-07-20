/*
 * sensor.c
 *
 * Sensor-/Mess-Ebene (siehe sensor.h). Aus main.c herausgezogen;
 * Verhalten unveraendert.
 */
#include "main.h"
#include "sensor.h"

/* CubeMX-Handles und App-Zustand - definiert in main.c. */
extern I2C_HandleTypeDef hi2c1;
extern DAC_HandleTypeDef hdac1;
extern volatile uint8_t error_mode;
extern calib_data EEPROM_values;
extern prod_param device_param;

void init_Sensor(void)
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

sensor_mess get_value(void)
{
	static sensor_mess last_good = {0, 0};	/* letzter gueltiger Messwert */
	sensor_mess mess_data;
	uint8_t rxBuffer[5] = {0};
	uint8_t start_Reg = 0x06;
	uint8_t tx_arr[2];
	tx_arr[0] = 0x30;
	tx_arr[1] = 0x0A;
	int32_t raw24, t16;
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

	raw24 = ((int32_t)rxBuffer[0] << 16) | ((int32_t)rxBuffer[1] << 8) | rxBuffer[2];
	if (rxBuffer[0] & 0x80)
	{
		raw24 -= 16777216;	/* 24-bit-Zweierkomplement */
	}
	t16 = ((int32_t)rxBuffer[3] << 8) | rxBuffer[4];
	if (rxBuffer[3] & 0x80)
	{
		t16 -= 65536;		/* 16-bit-Zweierkomplement */
	}

	/* Druck: raw/k mit k = 12,8 (40-kPa-Sensor) -> Integer: raw*10/128.
	 * (20-kPa-Sensor: k = 25,6 -> raw*10/256.) Vorher double -> zog die
	 * Soft-Float-Lib in den Flash, der M0+ hat keine FPU. */
	mess_data.pressure = (int32_t)(((int64_t)raw24 * 10) / 128) - EEPROM_values.offset;
	/* Temperatur: raw/256 Grad C -> 0,01 Grad C: raw*100/256 = raw*25/64 */
	mess_data.temp = (int16_t)((t16 * 25) / 64);

	last_good = mess_data;

	return mess_data;
}

uint16_t calc_percent(calib_data *datas, int64_t mw)
{

	if (datas->max_val == 0)
	{
		return 0;	/* Schutz: nie durch 0 teilen (defekte/leere Kalibrierung) */
	}
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
