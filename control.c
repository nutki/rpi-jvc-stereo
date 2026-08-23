#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

static struct gpiod_chip *chip;
static struct gpiod_line_settings *out_settings, *in_pull_up_settings, *in_pull_down_settings, *in_hiz_settings, *out_high_settings;
static struct gpiod_line_config *line_cfg, *kb_rows_cfg;
static struct gpiod_request_config *req_cfg;
static struct gpiod_line_request *request;
static struct gpiod_line_request *kb_rows_request;
static const unsigned int gpio_leds[] = {25, 22, 27};
static const unsigned int gpio_encoder_pins[] = {23, 24};
static const unsigned int gpio_jack_detect = 21;
static const unsigned int gpio_kb_rows[] = {12, 16, 20};
static const unsigned int num_kb_rows = sizeof(gpio_kb_rows) / sizeof(gpio_kb_rows[0]);
static const unsigned int gpio_kb_cols[] = {6, 13, 19, 26};
static const unsigned int num_kb_cols = sizeof(gpio_kb_cols) / sizeof(gpio_kb_cols[0]);
static const char *key_labels[3][4] = {
    {"1", "2", "3", "A"},
    {"4", "5", "6", "B"},
    {"7", "8", "9", "C"}
};
static int last_a = -1;
static int encoder_pos = 0;
static int key_state[3][4] = {{0}};
static int jack_detect_state = -1;

int control_init(void) {
    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("gpiod_chip_open");
        return 1;
    }

    out_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(out_settings, GPIOD_LINE_DIRECTION_OUTPUT);
    in_pull_up_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(in_pull_up_settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(in_pull_up_settings, GPIOD_LINE_BIAS_PULL_UP);
    in_pull_down_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(in_pull_down_settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(in_pull_down_settings, GPIOD_LINE_BIAS_PULL_DOWN);
    in_hiz_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(in_hiz_settings, GPIOD_LINE_DIRECTION_INPUT);
    out_high_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(out_high_settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(out_high_settings, GPIOD_LINE_VALUE_ACTIVE);

    line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, gpio_leds, 3, out_settings);
    gpiod_line_config_add_line_settings(line_cfg, gpio_encoder_pins, 2, in_pull_up_settings);    
    gpiod_line_config_add_line_settings(line_cfg, &gpio_jack_detect, 1, in_pull_up_settings);
    gpiod_line_config_add_line_settings(line_cfg, gpio_kb_cols, num_kb_cols, in_pull_down_settings);
    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "JVC front panel");
    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    kb_rows_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(kb_rows_cfg, gpio_kb_rows, num_kb_rows, in_hiz_settings);
    kb_rows_request = gpiod_chip_request_lines(chip, req_cfg, kb_rows_cfg);

    if (!request || !kb_rows_request) {
        perror("gpiod_chip_request_lines");
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        gpiod_line_request_set_value(request, gpio_leds[i], GPIOD_LINE_VALUE_ACTIVE);
    }
    return 0;
}
void close_control(void);

int main(void) {
    if (control_init()) {
        return 1;
    }
    for (int scan_counter = 0; !key_state[2][3]; scan_counter++) {
        int a = gpiod_line_request_get_value(request, gpio_encoder_pins[0]);
        int b = gpiod_line_request_get_value(request, gpio_encoder_pins[1]);       
        if (last_a == !a) {
            encoder_pos += b == a ? -1 : 1;
            printf("Position: %d (%sCW)\n", encoder_pos, b == a ? "C" : "");
        }
        last_a = a;
        
        int jack_val = !gpiod_line_request_get_value(request, gpio_jack_detect);
        if (jack_val != jack_detect_state) printf("Jack detect state: %d\n", jack_val);
        jack_detect_state = jack_val;
        
        if (scan_counter % 10 == 0) {
            struct gpiod_line_config *scan_cfg = gpiod_line_config_new();
            for (int row = 0; row < num_kb_rows; row++) {
                // Set active row to output high, others to input
                gpiod_line_config_reset(scan_cfg);
                for (int i = 0; i < num_kb_rows; i++) {
                    gpiod_line_config_add_line_settings(scan_cfg, &gpio_kb_rows[i], 1, i == row ? out_high_settings : in_hiz_settings);
                }
                gpiod_line_request_reconfigure_lines(kb_rows_request, scan_cfg);
                usleep(100);
                for (int col = 0; col < num_kb_cols; col++) {
                    int val = gpiod_line_request_get_value(request, gpio_kb_cols[col]);
                    if (val != key_state[row][col]) {
                        printf("Key %s: %s (row %d, col %d)\n", key_labels[row][col],  val ? "pressed" : "released", row, col);
                    }
                    key_state[row][col] = val;
                }
            }
            gpiod_line_request_reconfigure_lines(kb_rows_request, kb_rows_cfg);
            gpiod_line_config_free(scan_cfg);
        }
        usleep(1000);
    }
    close_control();
    return 0;
}

void close_control(void) {
    for (int i = 0; i < 3; i++) {
        gpiod_line_request_set_value(request, gpio_leds[i], GPIOD_LINE_VALUE_INACTIVE);
    }

    gpiod_line_request_release(request);
    gpiod_line_request_release(kb_rows_request);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_config_free(kb_rows_cfg);
    gpiod_line_settings_free(out_settings);
    gpiod_line_settings_free(in_pull_up_settings);
    gpiod_line_settings_free(in_pull_down_settings);
    gpiod_line_settings_free(out_high_settings);
    gpiod_line_settings_free(in_hiz_settings);
    gpiod_chip_close(chip);
}
