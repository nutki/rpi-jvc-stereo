#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(void)
{
    struct gpiod_chip *chip;
    struct gpiod_line_settings *out_settings, *in_settings, *kb_in_settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;
    struct gpiod_line_request *request;
    struct gpiod_line_request *kb_rows_request;

    unsigned int gpio_leds[] = {25, 22, 27};
    unsigned int gpio_enc_a = 23;
    unsigned int gpio_enc_b = 24;
    unsigned int gpio_button = 21;
    
    // Matrix keyboard: 3 rows (outputs), 4 columns (inputs)
    const unsigned int kb_rows[] = {12, 16, 20};
    const unsigned int num_kb_rows = sizeof(kb_rows) / sizeof(kb_rows[0]);
    const unsigned int kb_cols[] = {6, 13, 19, 26};
    const unsigned int num_kb_cols = sizeof(kb_cols) / sizeof(kb_cols[0]);
    const char *key_labels[3][4] = {
        {"1", "2", "3", "A"},
        {"4", "5", "6", "B"},
        {"7", "8", "9", "C"}
    };

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("gpiod_chip_open");
        return 1;
    }

    // Output settings for GPIO 27
    out_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(
        out_settings,
        GPIOD_LINE_DIRECTION_OUTPUT);

    // Input settings for GPIO 23 and 24 (encoder pins with pull-up)
    in_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(
        in_settings,
        GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(
        in_settings,
        GPIOD_LINE_BIAS_PULL_UP);
    
    // Input settings for keyboard columns (external pull-downs)
    kb_in_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(
        kb_in_settings,
        GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(
        kb_in_settings,
        GPIOD_LINE_BIAS_PULL_DOWN);

    line_cfg = gpiod_line_config_new();

    // Add output settings for LEDs (GPIO 25, 22, 27)
    gpiod_line_config_add_line_settings(
        line_cfg,
        gpio_leds,
        3,
        out_settings);

    // Add input settings for GPIO 23 and 24
    unsigned int encoder_pins[] = {gpio_enc_a, gpio_enc_b};
    gpiod_line_config_add_line_settings(
        line_cfg,
        encoder_pins,
        2,
        in_settings);
    
    // Add button input (GPIO 21 with pull-up)
    gpiod_line_config_add_line_settings(
        line_cfg,
        &gpio_button,
        1,
        in_settings);
    
    // Add keyboard column inputs
    gpiod_line_config_add_line_settings(
        line_cfg,
        kb_cols,
        num_kb_cols,
        kb_in_settings);

    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "control");

    request = gpiod_chip_request_lines(
        chip,
        req_cfg,
        line_cfg);

    // Create a single, separate request for keyboard rows, initially as inputs.
    struct gpiod_line_config *kb_rows_cfg = gpiod_line_config_new();
    struct gpiod_line_settings *kb_rows_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(kb_rows_settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_config_add_line_settings(kb_rows_cfg, kb_rows, num_kb_rows, kb_rows_settings);
    kb_rows_request = gpiod_chip_request_lines(chip, NULL, kb_rows_cfg);

    if (!request || !kb_rows_request) {
        perror("gpiod_chip_request_lines");
        return 1;
    }

    // Turn on all LEDs
    for (int i = 0; i < 3; i++) {
        gpiod_line_request_set_value(request, gpio_leds[i], GPIOD_LINE_VALUE_ACTIVE);
    }
    
    // Read encoder and keyboard for 1 second and display values
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int last_a = -1, last_b = -1;
    int encoder_pos = 0;
    int scan_counter = 0;
    
    // Track key states (0 = not pressed, 1 = pressed)
    int key_state[3][4] = {{0}};
    int button_state = -1;  // -1 = uninitialized
    
    printf("Reading rotary encoder and keyboard for 1 second...\n");
    printf("Position: %d\n", encoder_pos);
    
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + 
                        (now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= 150.0) break; // Increased time for testing
        
        int a = gpiod_line_request_get_value(request, gpio_enc_a);
        int b = gpiod_line_request_get_value(request, gpio_enc_b);
        
        if (a < 0 || b < 0) {
            perror("gpiod_line_request_get_value");
            break;
        }
        if (last_a != a || last_b != b) {
//            printf("Encoder A: %d, B: %d\n", a, b);
        }
        
        // Detect state changes
        if (last_a != -1 && last_b != -1) {
            // Check for rotation (simple quadrature decoding)
            if (last_a == 0 && a == 1) {
                if (b == 0) {
                    encoder_pos++;
                    printf("Position: %d (CW)\n", encoder_pos);
                } else {
                    encoder_pos--;
                    printf("Position: %d (CCW)\n", encoder_pos);
                }
            }
        }
        
        last_a = a;
        last_b = b;
        
        // Read button state (GPIO 21)
        int button_val = gpiod_line_request_get_value(request, gpio_button);
        if (button_val >= 0 && button_val != button_state) {
            button_state = button_val;
            // Button is active low (0 = pressed, 1 = released)
            if (button_val == 0) {
                printf("Button pressed\n");
            } else {
                printf("Button released\n");
            }
        }
        
        // Scan keyboard matrix every 10ms
        if (scan_counter % 10 == 0) {
            struct gpiod_line_config *scan_cfg = gpiod_line_config_new();
            struct gpiod_line_settings *output_high = gpiod_line_settings_new();
            gpiod_line_settings_set_direction(output_high, GPIOD_LINE_DIRECTION_OUTPUT);
            gpiod_line_settings_set_output_value(output_high, GPIOD_LINE_VALUE_ACTIVE);

            struct gpiod_line_settings *input_hiz = gpiod_line_settings_new();
            gpiod_line_settings_set_direction(input_hiz, GPIOD_LINE_DIRECTION_INPUT);

            for (int row = 0; row < num_kb_rows; row++) {
                gpiod_line_config_reset(scan_cfg);
                
                // Set active row to output high, others to input
                for (int i = 0; i < num_kb_rows; i++) {
                    if (i == row) {
                        gpiod_line_config_add_line_settings(scan_cfg, &kb_rows[i], 1, output_high);
                    } else {
                        gpiod_line_config_add_line_settings(scan_cfg, &kb_rows[i], 1, input_hiz);
                    }
                }
                gpiod_line_request_reconfigure_lines(kb_rows_request, scan_cfg);
                
                usleep(100); // Short delay for signal to settle
                
                // Read all columns
                for (int col = 0; col < num_kb_cols; col++) {
                    int val = gpiod_line_request_get_value(request, kb_cols[col]);
                    
                    // Check for state change
                    if (val != key_state[row][col]) {
                        key_state[row][col] = val;
                        if (val == 1) {
                            printf("Key pressed: %s (row %d, col %d)\n", 
                                   key_labels[row][col], row, col);
                        } else {
                            printf("Key released: %s (row %d, col %d)\n", 
                                   key_labels[row][col], row, col);
                        }
                    }
                }
            }
            
            // After scanning all rows, set all back to input
            gpiod_line_config_reset(scan_cfg);
            gpiod_line_config_add_line_settings(scan_cfg, kb_rows, num_kb_rows, input_hiz);
            gpiod_line_request_reconfigure_lines(kb_rows_request, scan_cfg);

            gpiod_line_config_free(scan_cfg);
            gpiod_line_settings_free(output_high);
            gpiod_line_settings_free(input_hiz);
        }
        
        scan_counter++;
        usleep(1000); // 1ms polling interval
    }
    
    printf("Final encoder position: %d\n", encoder_pos);

    // Turn off all LEDs
    for (int i = 0; i < 3; i++) {
        gpiod_line_request_set_value(request, gpio_leds[i], GPIOD_LINE_VALUE_INACTIVE);
    }

    gpiod_line_request_release(request);
    gpiod_line_request_release(kb_rows_request);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_config_free(kb_rows_cfg);
    gpiod_line_settings_free(out_settings);
    gpiod_line_settings_free(in_settings);
    gpiod_line_settings_free(kb_in_settings);
    gpiod_line_settings_free(kb_rows_settings);
    gpiod_chip_close(chip);

    return 0;
}
