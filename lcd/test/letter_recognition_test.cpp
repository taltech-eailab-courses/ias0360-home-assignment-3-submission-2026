#include <stdio.h>
#include <vector>
#include <cstring> 

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

#include "LCD_letter.h"
#include "request.hpp"
#include "sd_card_wrapper.hpp"

// TODO: Define SD card filename, request chunk size, and set your student ID.
#define SD_CARD_FILENAME "large_request_data_test.bin"
#define REQUEST_CHUNK_SIZE 1024
#define REQUEST_ID "YOUR_STUDENT_ID"

mutex_t mutex;
OCR_Inference inference;

static inline void debug_unpause_timer(void) {
    timer_hw->dbgpause = 0; 
}

// run core0 loop that displays UI and handle user interaction
void core1_entry() {
    while(true) {
        LCD_SetBackLight(1000);
        TP_DrawBoard();
    }
}

// TODO: Implement writing data to the SD card.
int write_data_to_sd_card(SD_CardWrapper &sd_card, uint8_t* data) {
    return 0;
}

// TODO: Implement reading data from SD card chunk by chunk with REQUEST_CHUNK_SIZE and sending it as a POST request to the server.
int read_data_from_sd_card_and_post_request(SD_CardWrapper &sd_card, HttpRequest *request) {
    return 0;
}

int main(void)
{
    System_Init();
    debug_unpause_timer();  
    
    mutex_init(&mutex);  // Initialize the mutex

    sleep_ms(5000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi chip init failed\n");
        return -1;
    }
    printf("Wi-Fi chip initialized successfully\n");

    cyw43_arch_enable_sta_mode();

    // Connect to the WiFI network - loop until connected
    printf("Start connecting wifi.\n");
    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000) != 0){
        printf("Attempting to connect...\n");
    }
    // Print a success message once connected
    printf("WiFi connected successfully! \n");
    
	LCD_SCAN_DIR  lcd_scan_dir = SCAN_DIR_DFT;
	Letter_Init(lcd_scan_dir, &inference);

    // run core1 loop that handles user interface
    multicore_launch_core1(core1_entry);

    HttpRequest request(URL_REQUEST);
    SD_CardWrapper sd_card;
	
	while(1) {
        uint32_t g = multicore_fifo_pop_blocking();
        // Acquire the mutex (blocking)
        mutex_enter_blocking(&mutex);  

		// Retrieve data from inference
        inference.state = PROCESSING;

        // Store data on SD card 
        printf("Writing data to SD card...\n");

        if (write_data_to_sd_card(sd_card, inference.data)) {
            printf("Failed to write data to SD card.\n");
            // Release the mutex
            inference.state = DEFAULT;
            mutex_exit(&mutex);
            continue;
        }
        printf("Data written to SD card.\n");

        // Retrieve data from SD card and send to server chunk by chunk
		printf("Sending a request...\n");
        
        if (read_data_from_sd_card_and_post_request(sd_card, &request)) {
            printf("Failed to send POST request with chunked body from SD card.\n");
        } else {
            printf("POST request with chunked body from SD card sent successfully.\n");
        }
        
		// Release the mutex
		inference.state = DEFAULT;
        //Return the resource
        mutex_exit(&mutex);
	}
	return 0;
}