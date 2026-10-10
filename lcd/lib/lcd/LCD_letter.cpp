#include <string.h>
#include <stdio.h>
#include <vector>
#include <cstdint>
#include "pico/multicore.h"

#include "LCD_letter.h"

static OCR_Inference* inference;
static TextField* text_field;
static Button* send_button;
static Button* clear_button;
static Button* menu_button;
static Button* back_button;
static Button* logout_button;

static bool is_menu_open = false;

TP_DEV sTP_DEV;
TP_DRAW sTP_Draw;

static const uint8_t sKernel[KERNEL_SIZE*KERNEL_SIZE] = {
    58, 44, 58,
    44, 76, 44,
    58, 44, 58
};

static bool prevent_interaction(void) 
{
    return (inference != NULL && (inference->state == PROCESSING || inference->state == SUCCESS));
}


void draw_ocr_prediction(void)
{
    if (inference != NULL && inference->response != NULL) 
    {
        int result_y0 = text_field->get_y() + BOX_H + 8;
        GUI_DrawRectangle(text_field->get_x(), result_y0,
                        text_field->get_x() + BOX_W, result_y0 + 36,
                        GREEN, DRAW_EMPTY, DOT_PIXEL_2X2);
        
        size_t response_length = strlen(inference->response);
        size_t prefix_length = strlen("Predicted: ");
        char* dynamic_display_str = (char*)malloc(response_length + prefix_length + 1);
        if (dynamic_display_str != NULL) {
            snprintf(dynamic_display_str, response_length + prefix_length + 1, "Predicted: %s", inference->response);
            GUI_DisString_EN(text_field->get_x() + 20, result_y0 + 8,
                                dynamic_display_str, &Font20, WHITE, BLACK);
            free(dynamic_display_str);
        }
        free(inference->response);
        inference->response = NULL;
    }
}

static void apply_blur(const uint8_t* src,
                       uint8_t* dst,
                       const uint8_t* kernel)
{
    const int r = KERNEL_SIZE / 2;
    uint8_t temp[DATA_SIZE_H][DATA_SIZE_W] = {0};

    // Convolution-like accumulation (binary foreground)
    for (int y = 0; y < DATA_SIZE_H; ++y) {
        for (int x = 0; x < DATA_SIZE_W; ++x) {
            if (src[(y + MARGIN) * BOX_W + (x + MARGIN)] == 0) continue;

            for (int j = y - r; j <= y + r; ++j) {
                if ((unsigned)j >= (unsigned)DATA_SIZE_H) continue;
                int ky = j - (y - r);

                for (int i = x - r; i <= x + r; ++i) {
                    if ((unsigned)i >= (unsigned)DATA_SIZE_W) continue;
                    int kx = i - (x - r);

                    unsigned int sum = temp[j][i] + kernel[ky * KERNEL_SIZE + kx];
                    temp[j][i] = (uint8_t)(sum > 255u ? 255u : sum);
                }
            }
        }
    }

    // 2×2 average downsample
    for (int j = 0; j < RESULT_H; ++j) {
        for (int i = 0; i < RESULT_W; ++i) {
            unsigned int s = 0;
            s += temp[j * 2 + 0][i * 2 + 0];
            s += temp[j * 2 + 0][i * 2 + 1];
            s += temp[j * 2 + 1][i * 2 + 0];
            s += temp[j * 2 + 1][i * 2 + 1];
            s >>= 2;
            if (s > 255u) s = 255u;
            dst[j * RESULT_W + i] = (uint8_t)s;
        }
    }
}

void on_send_button_pressed(void)
{
    if (prevent_interaction()) return;

    // text_field->print_data();
    printf("start processing.\r\n");
    apply_blur(text_field->data(), inference->data, sKernel);
    printf("blur applied.\r\n");
    TP_display_input(RESULT_H, RESULT_W, inference->data);
    // Trigger a http request
    multicore_fifo_push_blocking(OCR_MULTICORE_RUN_INFERENCE_FLAG);
}

void reset_user_input(void) 
{
    if (inference != NULL) {
        memset(inference->data, 0, sizeof(inference->data));
        free(inference->response);
        inference->response = NULL;
        inference->state = DEFAULT;
    }
    text_field->clear_data();
}

void reset_display(void) {
    LCD_Clear(LCD_BACKGROUND);

    TP_DrawHeader();

    menu_button->draw();
    send_button->draw();
    clear_button->draw();
    text_field->draw_box();
}

void on_clear_button_pressed(void)
{
    if (prevent_interaction()) return;

    reset_display();
    reset_user_input();
}

void on_menu_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Menu button pressed. (Not implemented)\n");
    reset_user_input();

    LCD_Clear(LCD_BACKGROUND);
    back_button->draw();
    logout_button->draw();
    is_menu_open = true;
}

void on_back_button_pressed(void)
{
    if (prevent_interaction()) return;
    printf("Back button pressed.\n");

    reset_display();
    is_menu_open = false;
}

void on_logout_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Logging out...\n");
    LCD_Clear(LCD_BACKGROUND);
    int screen_center_x = LCD_W / 2;
    int screen_center_y = LCD_H / 2;
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y, "Logged out!", &Font24, WHITE, BLACK);
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y + 26, "Goodbye!", &Font20, WHITE, BLACK);
    sleep_ms(1000);
    multicore_fifo_push_blocking(CORE1_EXIT_FLAG);
}

void TP_DrawBoard(void)
{
    if (prevent_interaction()) return;

    draw_ocr_prediction();
    TP_Scan(0);
    if (sTP_DEV.chStatus & TP_PRESS_DOWN)
    { 
        spi_init(SPI_PORT, 10000000);
        sTP_Draw.Color = BLACK;

        // printf("horizontal x:%d,y:%d\n", sTP_Draw.Xpoint, sTP_Draw.Ypoint);
        if (is_menu_open) {
            if (back_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Back button pressed\n");
                back_button->call_cb();
            }
            else if (logout_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Logout button pressed\n");
                logout_button->call_cb();
            }
        }
        else
        {
            if (clear_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            { 
                printf("Clear button pressed\n");
                clear_button->call_cb();
            }
            else if (send_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            { 
                printf("Send button pressed\n");
                send_button->call_cb();

            } 
            else if (menu_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Menu button pressed\n");
                menu_button->call_cb();
            }
            else if (text_field->is_box_touched(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                text_field->set_pixel(sTP_Draw.Xpoint, sTP_Draw.Ypoint);
                GUI_DrawPoint(sTP_Draw.Xpoint, sTP_Draw.Ypoint, BLACK, DOT_PIXEL_5X5, DOT_FILL_RIGHTUP);
            }
        }
        spi_init(SPI_PORT, 5000000);
    }
    SPI4W_Write_Byte(0xFF);
}


void Letter_Init(LCD_SCAN_DIR lcd_scan_dir, OCR_Inference* _inference)
{
    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &sTP_DEV, &sTP_Draw);

    TP_GetAdFac();

    inference = _inference;
    inference->state = DEFAULT;

    TP_DrawHeader();

    send_button = new Button(SEND_X0, SEND_Y0, BUTTON_W, BUTTON_H, "SEND", DOT_PIXEL_2X2, BLUE, WHITE, 22, 13);
    send_button->set_callback(on_send_button_pressed);
    clear_button = new Button(CLEAR_X0, CLEAR_Y0, BUTTON_W, BUTTON_H, "CLEAR", DOT_PIXEL_2X2, RED, WHITE, 18, 13);
    clear_button->set_callback(on_clear_button_pressed);

    menu_button = new Button(MENU_X0, MENU_Y0, 60, BUTTON_H, "MENU", DOT_PIXEL_2X2, GREEN, WHITE, 4, 13);
    menu_button->set_callback(on_menu_button_pressed);

    back_button = new Button(BACK_X0, BACK_Y0, MENU_BUTTON_W, BUTTON_H, "BACK", DOT_PIXEL_2X2, BLACK, WHITE, 30, 13);
    back_button->set_callback(on_back_button_pressed);

    logout_button = new Button(LOGOUT_X0, LOGOUT_Y0, MENU_BUTTON_W, BUTTON_H, "LOGOUT", DOT_PIXEL_2X2, RED, WHITE, 24, 13);
    logout_button->set_callback(on_logout_button_pressed);


    text_field = new TextField(BOX_X0, BOX_Y0, BOX_W, BOX_H);

    reset_display();
}
