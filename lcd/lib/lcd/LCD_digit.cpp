#include <string.h>
#include <stdio.h>
#include "pico/multicore.h"

#include "LCD_digit.h"

#define KERNEL_SIZE 3

static DigitInference_t* inference;
static TextField text_fields[DIGIT_INPUT_COUNT];
static Button* login_button;
static Button* clear_button;

TP_DEV sTP_DEV;
TP_DRAW sTP_Draw;

static const uint8_t sKernel[KERNEL_SIZE*KERNEL_SIZE] = {
    51, 25, 51,
    25, 76, 25,
    51, 25, 51
};

static bool prevent_interaction(void) 
{
    return (inference != NULL && (inference->state == PROCESSING || inference->state == SUCCESS));
}

void TP_DrawPredictedResult(void) 
{
    if (prevent_interaction()) return;

    // Show result
    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        if (inference->UserInputs[i].PredictedDigit >= 0) {
            char result[25] = "Predicted: ";
            char temp[2];
            if (inference->UserInputs[i].PredictedDigit == UNKNOWN_PREDICTION) {
                sprintf(temp, "%s", "-");
            } else {
                sprintf(temp, "%d", inference->UserInputs[i].PredictedDigit);
            }
            strcat(result, temp);
            printf("Predicted result for input %d: %s\n", i, result);
            GUI_DisString_EN(text_fields[i].get_x() + 2, text_fields[i].get_y() + BOX_SIZE + 12, result, &Font12, WHITE, BLACK);
        }
    }
}

void draw_expected_input(void)
{
    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        char expected[25] = "Expected: ";
        char temp[2];
        sprintf(temp, "%c", inference->UserInputs[i].ExpectedDigit);
        strcat(expected, temp);

        GUI_DisString_EN(text_fields[i].get_x() + 2, text_fields[i].get_y() + BOX_SIZE + 2, expected, &Font12, WHITE, BLACK);
    }
}

void reset_user_input(UserInput_t* input, int index) 
{
    if (input != NULL) {
        memset(input->InputData, 0, sizeof(input->InputData));
        input->PredictedDigit = -1;
        char digit = FIRST_FOUR_DIGIT_STUDENT_CODE[index];
        input->ExpectedDigit = digit; // set expected digit
    }
}


void on_login_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Start processing.\r\n");
    // TODO: Apply any preprocessing steps to the input data before sending the HTTP request.
    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        printf("Input %d:\n", i);
        text_fields[i].print_data();
        
        TP_display_input(INPUT_IMAGE_SIZE, INPUT_IMAGE_SIZE, inference->UserInputs[i].InputData);
    }

    // Send a http request
    multicore_fifo_push_blocking(MULTICORE_RUN_INFERENCE_FLAG);
}

void reset_display(void) {
    LCD_Clear(LCD_BACKGROUND);
    
    TP_DrawHeader();

    login_button->draw();
    clear_button->draw();

    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        text_fields[i].draw_box();
    }
    draw_expected_input();
}

void on_clear_button_pressed(void)
{
    if (prevent_interaction()) return;


    // NEW: also clear the shadow buffer that mirrors what's drawn
    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        reset_user_input(&inference->UserInputs[i], i);
        text_fields[i].clear_data();
    }
    inference->state = DEFAULT;
    reset_display();
}


void reset_inference(DigitInference_t* _inference) 
{
    if (_inference != NULL) {
        inference = _inference;
    }
    inference->state = DEFAULT;
    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        reset_user_input(&inference->UserInputs[i], i);
    }
}

void TP_DrawDigitBoard(void)
{
    if (prevent_interaction()) return;
    
    TP_Scan(0);
    spi_init(SPI_PORT, 10000000);
    // printf("horizontal x:%d,y:%d\n", sTP_Draw.Xpoint, sTP_Draw.Ypoint);

    if (sTP_DEV.chStatus & TP_PRESS_DOWN) {
        sTP_Draw.Color = BLACK;

        if (login_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
        { 
            printf("Login button pressed.\n");
            login_button->call_cb();
        }
        else if (clear_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
        {
            printf("Clear button pressed.\n");
            clear_button->call_cb();
        }
        else if (sTP_Draw.Ypoint > BOX_START_Y && sTP_Draw.Ypoint < BOX_START_Y + BOX_SIZE)
        {
            for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
                if (text_fields[i].is_box_touched(sTP_Draw.Xpoint, sTP_Draw.Ypoint)) {
                    // register in bitmapped array
                    text_fields[i].set_pixel(sTP_Draw.Xpoint, sTP_Draw.Ypoint);
                    // draw on LCD
                    GUI_DrawPoint(sTP_Draw.Xpoint, sTP_Draw.Ypoint, BLACK, DOT_PIXEL_5X5, DOT_FILL_RIGHTUP);
                    break;
                }
            }
        }
        
    spi_init(SPI_PORT, 5000000);
    }
    SPI4W_Write_Byte(0xFF);
}

void TP_DrawSuccess(void)
{
    LCD_Clear(LCD_BACKGROUND);
    TP_DrawHeader();
    int screen_center_x = LCD_W / 2;
    int screen_center_y = LCD_H / 2;
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y,
                     "Login Success!", &Font24, WHITE, GREEN);
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y + 30,
                     "Redirecting in 3 seconds...", &Font16, WHITE, BLACK);
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y + 45,
                     "Stay connected.", &Font16, WHITE, BLACK);
}

void Digit_Init(LCD_SCAN_DIR lcd_scan_dir, DigitInference_t* _inference)
{
    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &sTP_DEV, &sTP_Draw);

    TP_GetAdFac();

    inference = _inference;
    inference->state = DEFAULT;

    TP_DrawHeader();

    login_button = new Button(LOGIN_BUTTON_X0, LOGIN_BUTTON_Y0, BUTTON_W, BUTTON_H, "LOGIN", DOT_PIXEL_2X2, BLUE, WHITE, 24, 15);
    login_button->set_callback(on_login_button_pressed);
    clear_button = new Button(CLEAR_BUTTON_X0, CLEAR_BUTTON_Y0, BUTTON_W, BUTTON_H, "CLEAR", DOT_PIXEL_2X2, RED, WHITE, 24, 15);
    clear_button->set_callback(on_clear_button_pressed);

    uint16_t box_start_x = (LCD_W - BOX_TOTAL_W) / 2;

    for (int i=0; i<DIGIT_INPUT_COUNT; i++) {
        uint16_t x = box_start_x + i * (BOX_SIZE + 2 * BORDER_THICKNESS + SPACE_BETWEEN_BOXES) - BORDER_THICKNESS;
        uint16_t y = BOX_START_Y - BORDER_THICKNESS;
        text_fields[i] = TextField(
            x,
            y,
            BOX_SIZE,
            BOX_SIZE
        );
        reset_user_input(&inference->UserInputs[i], i);
    }

    reset_display();
}
