/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "nmea2000.h"
#include "ble.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* Quittung fuer einen ausgefuehrten Werksreset: rot, gruen, blau, weiss
 * kurz hintereinander. Ausser der LED hat das Geraet nichts, womit es
 * "erledigt" melden koennte - Bus und Funk sind im selben Moment weg. */
void led_factory_pattern(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BLE_MODE_Pin GPIO_PIN_9
#define BLE_MODE_GPIO_Port GPIOB
#define BLE_RESET_Pin GPIO_PIN_5
#define BLE_RESET_GPIO_Port GPIOA
#define BLE_BUSY_Pin GPIO_PIN_1
#define BLE_BUSY_GPIO_Port GPIOB
#define BLE_LED_Pin GPIO_PIN_2
#define BLE_LED_GPIO_Port GPIOB
#define WC_N_Pin GPIO_PIN_15
#define WC_N_GPIO_Port GPIOA
#define D_OUT_Pin GPIO_PIN_3
#define D_OUT_GPIO_Port GPIOB
#define CAN_STBY_Pin GPIO_PIN_4
#define CAN_STBY_GPIO_Port GPIOB
#define TASTER_Pin GPIO_PIN_8
#define TASTER_GPIO_Port GPIOB
#define TASTER_EXTI_IRQn EXTI4_15_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
