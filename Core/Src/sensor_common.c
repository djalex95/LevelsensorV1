/*
 * sensor_common.c
 *
 * Variantenneutraler Teil der Sensor-/Mess-Ebene: Prozentrechnung,
 * Tankform-Linearisierung und Analogausgang (DAC). Aus sensor.c
 * herausgezogen; Verhalten unveraendert.
 *
 * Das Einlesen des Rohwerts steckt variantenabhaengig in
 * sensor_legacy.c bzw. sensor_pdms.c - das Makefile bindet genau eine
 * der beiden Dateien ein.
 */
#include "main.h"
#include "sensor.h"

/* CubeMX-Handles und App-Zustand - definiert in main.c. */
extern DAC_HandleTypeDef hdac1;
extern prod_param device_param;

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
