#ifndef __LCD_LETTER_H_
#define __LCD_LETTER_H_

#include "LCD_utils.hpp"
#include "DEV_Config.h"
#include "LCD_Touch.h"


#define BUTTON_W 100
#define BUTTON_H 40
#define RESULT_TEXT_W 360
#define RESULT_TEXT_H 50

#define MARGIN 10
#define MARGIN_2 2*MARGIN
#define BOX_X0 60
#define BOX_Y0 80
#define BOX_X1 420   // right  edge (exclusive in our math)
#define BOX_Y1 200   // bottom edge (exclusive in our math)

#define DATA_SIZE_W (BOX_X1 - BOX_X0) // 360
#define DATA_SIZE_H (BOX_Y1 - BOX_Y0) // 120

#define BOX_W (MARGIN_2 + DATA_SIZE_W)   // 360 + 20
#define BOX_H (MARGIN_2 + DATA_SIZE_H)   // 120 + 20

#define CLEAR_X1 (LCD_W / 2) - MARGIN
#define CLEAR_Y1 LCD_H - MARGIN
#define CLEAR_X0 CLEAR_X1 - BUTTON_W
#define CLEAR_Y0 CLEAR_Y1 - BUTTON_H

#define SEND_X0 ((LCD_W / 2) + MARGIN)
#define SEND_X1 (SEND_X0 + BUTTON_W)
#define SEND_Y0 CLEAR_Y0
#define SEND_Y1 CLEAR_Y1

#define MENU_X0 10
#define MENU_Y0 10
#define MENU_W 30	
#define MENU_H 30

#define MENU_BUTTON_W (BUTTON_W + 30)
#define BACK_X0 (LCD_W / 2 - (MENU_BUTTON_W / 2))
#define BACK_Y0 (LCD_H / 2 - (BUTTON_H / 2) - 30)

#define LOGOUT_X0 BACK_X0
#define LOGOUT_Y0 (LCD_H / 2 - (BUTTON_H / 2) + 30)

#define RESULT_W 180
#define RESULT_H 60
#define DATA_SIZE (RESULT_W*RESULT_H)

#define KERNEL_SIZE 3

#define CORE1_EXIT_FLAG 0xDEAD
#define OCR_MULTICORE_RUN_INFERENCE_FLAG 111


typedef struct {
	State state;
	uint8_t data[DATA_SIZE];
	char* response;
} OCR_Inference;


void Letter_Init(LCD_SCAN_DIR lcd_scan_dir, OCR_Inference* _inference);


#endif