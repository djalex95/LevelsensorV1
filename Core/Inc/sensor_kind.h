/*
 * sensor_kind.h
 *
 * Kennungen der Sensor-Bauarten. Getrennt von sensor_cfg.h, damit die
 * variantenspezifische sensor_cfg.h die Kennung symbolisch setzen kann,
 * ohne von der Include-Reihenfolge abzuhaengen.
 */
#ifndef INC_SENSOR_KIND_H_
#define INC_SENSOR_KIND_H_

/* Alter Drucksensor (24 Bit, I2C 0x6D, Messung per Kommando angestossen). */
#define SENSOR_KIND_LEGACY  1

/* Wuerth WSEN-PDMS (16 Bit, I2C 0x6C, freilaufend alle 2,4 ms). */
#define SENSOR_KIND_PDMS    2

#endif /* INC_SENSOR_KIND_H_ */
