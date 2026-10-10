#include <cmath> 
#include<iostream>
#include <cstdlib> 
#include <iostream>
#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

extern "C" {
    #include "LCD_Driver.h"
    #include "LCD_Touch.h"
    #include "LCD_GUI.h"
    #include "DEV_Config.h"
}

#include "model.h"
#include "context.h"
#include "LCD_digit.h"
#include "model_settings.h"
#include "mnist_model_data.h"

using namespace std; 
#define HALT_CORE_1() while (1) { tight_loop_contents(); }
#define ONE_SECOND_IN_US 1000000

DigitInference_t inference;
Model ml_model;
mutex_t mutex;  // Declare a mutex

int count_digits(int number) {
    if (number == 0) {
        return 1; // Special case for 0, which has 1 digit
    }

    number = abs(number); // Handle negative numbers
    return std::floor(std::log10(number)) + 1;
}


// run core0 loop that displays UI and handle user interaction
void core1_entry() {

    absolute_time_t start_time;
    bool timer_started = false;
    bool message_displayed = false;

    while(true) {
		LCD_SetBackLight(1000);
        TP_DrawDigitBoard();
        TP_DrawPredictedResult();

        if (inference.state == SUCCESS) {
            if (!timer_started) {
                // Mark the start time
                start_time = get_absolute_time();
                timer_started = true;
            }
            
            if (absolute_time_diff_us(start_time, get_absolute_time()) >= ONE_SECOND_IN_US) {
                // Display success message after half a second
                if (!message_displayed) {
                    TP_DrawSuccess();
                    message_displayed = true;
                }
            }

            if (absolute_time_diff_us(start_time, get_absolute_time()) >= 5 * ONE_SECOND_IN_US) {
                printf("Successfully logged in!\n");
                LCD_Clear(LCD_BACKGROUND);
                multicore_fifo_push_blocking(CORE1_EXIT_FLAG);
                break;
            }
        }
    }

    printf("Core1 done, halting...\n");
    HALT_CORE_1();
}

int main(void) 
{
    System_Init();
    mutex_init(&mutex);  // Initialize the mutex

    sleep_ms(3000);

    LCD_SCAN_DIR  lcd_scan_dir = SCAN_DIR_DFT;
    Digit_Init(lcd_scan_dir, &inference);

    // run core1 loop that handles user interface
    multicore_launch_core1(core1_entry);

        // initialize ML model
    if (!ml_model.setup()) {
        printf("Failed to initialize ML model!\n");
        HALT_CORE_1();
    }
    printf("Model initialized\n");
    
    uint8_t* test_image_input = ml_model.input_data();
    if (test_image_input == nullptr) {
        printf("Cannot set input\n");
        HALT_CORE_1();
    }
    
    int byte_size = ml_model.byte_size();
    if (!byte_size) {
        printf("Byte size not found\n");
        HALT_CORE_1();
    }

    while (true) {

        // Block the process until data being filled
        uint32_t g = multicore_fifo_pop_blocking();
        if (g == CORE1_EXIT_FLAG) {
            break; // Exit the loop if the exit flag is received
        }

        // Acquire the mutex (blocking)
        mutex_enter_blocking(&mutex);  

        inference.state = PROCESSING;

        bool success = true;
        // Run inference and set predicted result
        // This for loop is used for debugging purpose.
        for (int index=0; index<DIGIT_INPUT_COUNT; index++) {
            for (int i=0; i<INPUT_IMAGE_SIZE; i++) {
                for (int j=0; j<INPUT_IMAGE_SIZE; j++) {
                    uint8_t num = inference.UserInputs[index].InputData[i*INPUT_IMAGE_SIZE + j];
                    int space = 3 - count_digits(num);
                    printf("%d", num);
                    for (int i = 0; i < space; ++i) {
                        printf(" ");
                    }
                }
                printf("\n");
            }

            memcpy(test_image_input, inference.UserInputs[index].InputData, byte_size);

            absolute_time_t inference_start = get_absolute_time();

            int result = ml_model.predict();

            int64_t inference_time_us = absolute_time_diff_us(inference_start, get_absolute_time());
            float inference_time_ms = inference_time_us / 1000.0f;
            if (result == -1) {
                printf("Failed to run inference\n");
                inference.UserInputs[index].PredictedDigit = UNKNOWN_PREDICTION;
            } else {
                printf("Predicted: %d\n", result);
                printf("Inference time: %.2f ms\n", inference_time_ms);
                inference.UserInputs[index].PredictedDigit = result;
            }
            if (inference.UserInputs[index].PredictedDigit != inference.UserInputs[index].ExpectedDigit - '0') {
                success = false;
            }
            sleep_ms(200);
        }

        // if (success) {
        //     inference.state = SUCCESS;
        // } else {
        //     inference.state = DEFAULT;
        // }
        inference.state = SUCCESS;
        printf("Login process finished.\n");

        //Return the resource
        mutex_exit(&mutex);

    }

    printf("Successfully logged in!\n");
    mutex_exit(&mutex);
    multicore_reset_core1();
    sleep_ms(1000);

    printf("Jumping to the OCR app...\n");
    jump_to_image(OCR_APP_OFFSET);
    return 0;
}
