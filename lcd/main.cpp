#include "pico/stdlib.h"

#define LED_RED   2   // change to your GPIO
#define LED_BLUE  3   // change to your GPIO
#define BTN_RED   4
#define BTN_BLUE  5

typedef enum { MODE_RED, MODE_BLUE } mode_t;

int main() {
    stdio_init_all();
    gpio_init(LED_RED); gpio_set_dir(LED_RED, true);
    gpio_init(LED_BLUE); gpio_set_dir(LED_BLUE, true);
    gpio_init(BTN_RED); gpio_set_dir(BTN_RED, false);
    gpio_pull_up(BTN_RED);
    gpio_init(BTN_BLUE); gpio_set_dir(BTN_BLUE, false);
    gpio_pull_up(BTN_BLUE);

    mode_t mode = MODE_RED;
    absolute_time_t next_toggle = delayed_by_ms(get_absolute_time(), 500);

    bool led_state = false;

    while (true) {
        // Button logic: active-low if using pull-ups
        if (!gpio_get(BTN_RED))  mode = MODE_RED;
        if (!gpio_get(BTN_BLUE)) mode = MODE_BLUE;

        if (absolute_time_diff_us(get_absolute_time(), next_toggle) <= 0) {
            led_state = !led_state;
            gpio_put(LED_RED,  (mode == MODE_RED)  ? led_state : false);
            gpio_put(LED_BLUE, (mode == MODE_BLUE) ? led_state : false);
            next_toggle = delayed_by_ms(get_absolute_time(), 500);
        }
    }
}