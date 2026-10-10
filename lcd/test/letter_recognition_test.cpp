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

#define SD_CARD_FILENAME "large_request_data_test.bin"
#define REQUEST_CHUNK_SIZE 1024
#define REQUEST_ID "id_00002"

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

int write_data_to_sd_card(SD_CardWrapper &sd_card, uint8_t* data) {
    // Save large data to SD card for later use

    if (!sd_card.isMounted()) {
        printf("[Writer] SD card not mounted, cannot write data\n");
        return -1;
    }

    // filename to store large request data
    size_t offset = 0;
    size_t total_packets_count = (DATA_SIZE + REQUEST_CHUNK_SIZE - 1) / REQUEST_CHUNK_SIZE;

    // create a file in SD card
    if (!sd_card.createFile(SD_CARD_FILENAME)) {
        printf("[Writer] Failed to create file on SD card\n");
        return -1;
    }

    sleep_ms(100);  // optional flush delay

    // erase the content if any
    if (!sd_card.clearFile(SD_CARD_FILENAME)) {
        printf("[Writer] Failed to clear existing file content on SD card\n");
        return -1;
    }

    // write data in chunks
    bool success = true;
    while (offset < DATA_SIZE) {
        size_t remaining = DATA_SIZE - offset;
        size_t chunk_elems = (remaining > REQUEST_CHUNK_SIZE) ? REQUEST_CHUNK_SIZE : remaining;

        if (!sd_card.appendToFile(SD_CARD_FILENAME, data + offset, chunk_elems)) {
            printf("[Writer] Failed to append data chunk to SD card\n");
            success = false;
            break;
        }
        
        offset += chunk_elems;

        sleep_ms(100);  // optional flush delay
    }
    if (!success) {
        printf("[Writer] Data write to SD card failed.\n");
        return -1;
    }

    printf("[Writer] Large data written to SD card successfully\n");

    size_t file_size = sd_card.getFileSize(SD_CARD_FILENAME);
    printf("Written file size: %zu bytes\n", file_size);
    if (file_size != DATA_SIZE) {
        printf("File size mismatched. Check the file content.\n");
    }

    printf("[Writer] Data write to SD card completed.\n");

    return 0;
}


int read_data_from_sd_card_and_post_request(SD_CardWrapper &sd_card, HttpRequest *request) {

    if (!sd_card.isMounted()) {
        printf("SD card not mounted, cannot read data\n");
        return -1;
    }
    printf("[Reader] SD card mounted successfully for reading data.\n");
    sleep_ms(100);  // optional flush delay

    // get total file size from SD card
    size_t file_size = sd_card.getFileSize(SD_CARD_FILENAME);
    printf("[Reader] Reading file of size: %zu bytes\n", file_size);

    size_t offset = 0;
    size_t total_packets_count = (DATA_SIZE + REQUEST_CHUNK_SIZE - 1) / REQUEST_CHUNK_SIZE;
    std::vector<uint8_t> stored_data;
    
    // Pre-allocate buffer for JSON to avoid repeated allocations
    std::string json_str;
    json_str.reserve(REQUEST_CHUNK_SIZE * 5); // Reserve enough space


    for (int packet_index = 0; packet_index < total_packets_count; packet_index++) {
        size_t remaining = file_size - offset;
        size_t chunk_elems = (remaining > REQUEST_CHUNK_SIZE) ? REQUEST_CHUNK_SIZE : remaining;

        stored_data.clear();
        stored_data.resize(chunk_elems);

        // read chunked data from SD card
        if (!sd_card.readChunkedFile(SD_CARD_FILENAME, stored_data, chunk_elems, offset)) {
            printf("[Reader] Failed to read data chunk from SD card\n");
            return -1;
        }
        printf("[Reader] Read chunk of size %zu bytes from offset %zu\n", stored_data.size(), offset);

        // Build JSON string efficiently without ostringstream
        json_str.clear(); // Reuse the same string buffer
        json_str.append("{\"x\": \"");
        
        // Manually build the comma-separated values without ostringstream
        char buffer[16];
        for (size_t i = 0; i < stored_data.size(); i++) {
            snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(stored_data[i]));
            json_str.append(buffer);
            if ((REQUEST_CHUNK_SIZE * packet_index) + i < DATA_SIZE - 1) {
                json_str.append(",");
            }
        }
        
        json_str.append("\", \"request_id\": \"");
        json_str.append(REQUEST_ID);
        json_str.append("\", \"packet_index\": ");
        snprintf(buffer, sizeof(buffer), "%d", packet_index);
        json_str.append(buffer);
        json_str.append(", \"total_packets\": ");
        snprintf(buffer, sizeof(buffer), "%zu", total_packets_count);
        json_str.append(buffer);
        json_str.append("}");

        request->set_body_raw(json_str.c_str(), json_str.size());
        ResponseDataStr response = request->post();
        
        if (!response.status_ok) {
            printf("[Reader] Chunk send failed at packet index %d.\n", packet_index);
            // Free response memory before continuing
            if (response.y) {
                free(response.y);
                response.y = NULL;
            }
            sleep_ms(100);
            continue;
        }

        if (packet_index == total_packets_count - 1) {
            printf("[Reader] Sent last packet %d/%zu\n", packet_index + 1, total_packets_count);
            printf("[Reader] Response: %s\n", response.y ? response.y : "No response");

            // Free old response before allocating new one
            if (inference.response) {
                free(inference.response);
                inference.response = NULL;
            }

            // Allocate new response only if we have data
            if (response.y) {
                size_t response_length = strlen(response.y);
                inference.response = (char*)malloc(response_length + 1);
                if (inference.response) {
                    strncpy(inference.response, response.y, response_length);
                    inference.response[response_length] = '\0';
                }
            }
        } else {
            printf("[Reader] Sent packet %d/%zu\n", packet_index + 1, total_packets_count);
        }

        // Free response memory BEFORE reinitializing request
        if (response.y) {
            free(response.y);
            response.y = NULL;
        }
        
        request->reinitialize();
        sleep_ms(300);
        
        offset += chunk_elems;
    }

    // Clear vectors to free memory
    stored_data.clear();
    stored_data.shrink_to_fit(); // Release the memory back to system
    json_str.clear();
    json_str.shrink_to_fit();

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
            // Release the mutex
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