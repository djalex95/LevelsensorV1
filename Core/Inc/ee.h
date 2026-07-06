/*
 * ee.h
 *
 *  Created on: Aug 29, 2023
 *      Author: haeckelmoser
 */

#ifndef INC_EE_H_
#define INC_EE_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdbool.h>
#include "main.h"

//################################################################################################################
bool      ee_init(void);
bool      ee_format(bool keepRamData);
bool      ee_read(uint32_t startVirtualAddress, uint32_t len, uint8_t* data);
bool      ee_write(uint32_t startVirtualAddress, uint32_t len, uint8_t* data);
bool      ee_writeToRam(uint32_t startVirtualAddress, uint32_t len, uint8_t* data); //  only use when _EE_USE_RAM_BYTE is enabled
bool      ee_commit(void);  //  only use when _EE_USE_RAM_BYTE is enabled
uint32_t  ee_maxVirtualAddress(void);

//################################################################################################################

#ifdef __cplusplus
}
#endif


#endif /* INC_EE_H_ */
