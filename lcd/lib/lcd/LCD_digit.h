#ifndef __LCD_DIGIT_H_
#define __LCD_DIGIT_H_

#include "LCD_utils.hpp"
#include "DEV_Config.h"
#include "LCD_Touch.h"

#define IGNORE_INTERVAL_MS 200
#define DIGIT_INPUT_COUNT 4
#define SPACE_BETWEEN_BOXES 10
#define BORDER_THICKNESS 2
#define BOX_SIZE 84
#define BOX_TOTAL_W (DIGIT_INPUT_COUNT * (BOX_SIZE + 2 * BORDER_THICKNESS) + (DIGIT_INPUT_COUNT-1) * SPACE_BETWEEN_BOXES)
#define BOX_START_Y 120  // Starting Y position for the boxes
#define BOX_PADDING 4
#define WINDOW_SIZE 3
#define INPUT_IMAGE_SIZE 28
#define UNKNOWN_PREDICTION 100

#define BUTTON_W 120
#define BUTTON_H 40
#define CLEAR_BUTTON_X0 (LCD_W / 2 - BUTTON_W - 20)
#define CLEAR_BUTTON_Y0 (LCD_H - BUTTON_H - 10)
#define LOGIN_BUTTON_X0 (LCD_W / 2 + 20)
#define LOGIN_BUTTON_Y0 (LCD_H - BUTTON_H - 10)

#define CORE1_EXIT_FLAG 0xDEAD
#define MULTICORE_RUN_INFERENCE_FLAG 123



#define FIRST_FOUR_DIGIT_STUDENT_CODE "2514"

typedef struct{
	uint8_t InputData[INPUT_IMAGE_SIZE * INPUT_IMAGE_SIZE]; // 784 uint8_t array
	int8_t PredictedDigit;
	char ExpectedDigit; // student code
} UserInput_t;

typedef struct{
	State state;
	UserInput_t UserInputs[DIGIT_INPUT_COUNT]; // 4 inputs
} DigitInference_t;


void TP_DrawDigitBoard(void);
void TP_DrawSuccess(void);
void TP_DrawPredictedResult(void);
void Digit_Init(LCD_SCAN_DIR Lcd_ScanDir, DigitInference_t* _inference);

#endif