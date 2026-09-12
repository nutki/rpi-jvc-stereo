#include <gpiod.h>
#include <stdio.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include "control.h"

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
static int last_a = -1;
static int encoder_pos = 0;
static int key_state[3][4] = {{0}};
static int jack_detect_state = -1;

static int ir_rx_fd = -1;
static int ir_tx_fd = -1;
static int ir_last_code = -1;
static int ir_last_toggle = -1;

#define IR_TECHNICS_COUNT (2 + (48 * 2) + 1)
static int ir_technics_encode(uint64_t code, uint32_t *buffer, size_t capacity) {
    uint8_t xor_result;

    if (capacity < IR_TECHNICS_COUNT) return -1;

    if (code < UINT64_C(0x1000000))
        code += UINT64_C(0xbffbfa000000);

    xor_result = (uint8_t)code ^ (uint8_t)(code >> 8) ^
                 (uint8_t)(code >> 16) ^ (uint8_t)(code >> 24);
    if (xor_result != 0) {
        fprintf(stderr, "Warning: correcting the last byte for XOR checksum -> %08lx\n", code);
        code ^= xor_result;
    }

    buffer[0] = 3550;
    buffer[1] = 1650;
    for (size_t bit = 0; bit < 48; bit++) {
        buffer[2 + bit * 2] = 497;
        buffer[3 + bit * 2] = (code & (UINT64_C(1) << (47 - bit))) ? 347 : 1218;
    }
    buffer[IR_TECHNICS_COUNT - 1] = 497;
    return IR_TECHNICS_COUNT;
}
#define IR_THOMSON_COUNT 12*2 + 1
static int ir_thomson_encode(uint64_t code, uint32_t *buffer, size_t capacity) {
    if (capacity < IR_THOMSON_COUNT) return -1;
    buffer[0] = 600 - 250;
    for (size_t bit = 0; bit < 12; bit++) {
        buffer[bit * 2 + 1] = (code & (1 << (11-bit))) ? 4525 + 250 : 1975 + 250;
        buffer[bit * 2 + 2] = 600 - 250;
    }
    return IR_THOMSON_COUNT;
}


int ir_init(void) {
    ir_rx_fd = open("/dev/lirc1", O_RDONLY | O_NONBLOCK);
    if (ir_rx_fd < 0) return 1;
    unsigned int protos = LIRC_MODE_SCANCODE;
    if (ioctl(ir_rx_fd, LIRC_SET_REC_MODE, &protos)) return 1;
    ir_tx_fd = open("/dev/lirc0", O_WRONLY);
    if (ir_tx_fd < 0) {
        perror("open");
        return 1;
    }
    unsigned int mode = LIRC_MODE_PULSE;
    if (ioctl(ir_tx_fd, LIRC_SET_SEND_MODE, &mode) < 0) {
        perror("LIRC_SET_SEND_MODE");
        close(ir_tx_fd);
        return 1;
    }
    return 0;
}
int ir_rx_read() {
    struct lirc_scancode sc;
    int n = read(ir_rx_fd, &sc, sizeof(sc)) == sizeof(sc);
    if (n) {
        int is_repeat = sc.flags & LIRC_SCANCODE_FLAG_REPEAT ? 1 : 0;
        if (sc.rc_proto == RC_PROTO_RC6_0) {
            if (sc.scancode == ir_last_code && (sc.flags & LIRC_SCANCODE_FLAG_TOGGLE) == ir_last_toggle) is_repeat = 1;
        }
        ir_last_code = sc.scancode;
        ir_last_toggle = sc.flags & LIRC_SCANCODE_FLAG_TOGGLE;
        return sc.scancode * 2 + is_repeat;
    }
    return -1;
}
void ir_close(void) {
    if (ir_rx_fd >= 0) close(ir_rx_fd);
    ir_rx_fd = -1;
    if (ir_tx_fd >= 0) close(ir_tx_fd);
    ir_tx_fd = -1;
}

void ir_tx_send(uint64_t code) {
    uint32_t buffer[IR_TECHNICS_COUNT];
    unsigned int carrier = 38000;
    if (ir_technics_encode(code, buffer, IR_TECHNICS_COUNT) < 0) {
        fprintf(stderr, "Unable to build IR pulse buffer\n");
        return;
    }
    if (ioctl(ir_tx_fd, LIRC_SET_SEND_CARRIER, &carrier) < 0) {
        perror("LIRC_SET_SEND_CARRIER");
        return;
    }
    if (write(ir_tx_fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        perror("write");
        return;
    }
}
void ir_tx_send_tv(uint64_t code) {
    uint32_t buffer[IR_THOMSON_COUNT];
    static int toggle = 0;
    unsigned int carrier = 33000;
    if (ir_thomson_encode(code | toggle, buffer, IR_THOMSON_COUNT) < 0) {
        fprintf(stderr, "Unable to build IR pulse buffer\n");
        return;
    }
    if (ioctl(ir_tx_fd, LIRC_SET_SEND_CARRIER, &carrier) < 0) {
        perror("LIRC_SET_SEND_CARRIER");
        return;
    }
    if (write(ir_tx_fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        perror("write");
        return;
    }
    toggle ^= 0x80;
}

void control_set_led(int led, int value) {
    if (led < 0 || led > 2) return;
    gpiod_line_request_set_value(request, gpio_leds[led], value & 1);
}

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

    if (ir_init()) {
        return 1;
    }
    return 0;
}
void close_control(void);

static const char *key_labels[12] = {
    "TA/NEWS/INFO",
    "EON ON/OFF",
    "DISPLAY MODE",
    "PTY SEARCH",
    "KEY MODE",
    "<",
    ">",
    "INPUT",
    "DIRECT",
    "S.A. BASS",
    "BAND",
    "STANDBY"
};

void control_event_loop(int (*event_callback)(int ev_type, int value)) {
    int stop = 0;
    if (control_init()) {
        return;
    }
    for (int scan_counter = 0; !stop; scan_counter++) {
        int ir = ir_rx_read();
        if (ir >= 0) event_callback(ir & 1 ? EVENT_REMOTE_REPEAT : EVENT_REMOTE_PRESSED, ir >> 1);
        int a = gpiod_line_request_get_value(request, gpio_encoder_pins[0]);
        int b = gpiod_line_request_get_value(request, gpio_encoder_pins[1]);       
        if (last_a == !a) {
            encoder_pos += b == a ? -1 : 1;
            stop = event_callback(b == a ? EVENT_ENCODER_MINUS : EVENT_ENCODER_PLUS, encoder_pos);
        }
        last_a = a;
        
        int jack_val = !gpiod_line_request_get_value(request, gpio_jack_detect);
        if (jack_val != jack_detect_state) stop = event_callback(EVENT_JACK_DETECT, jack_val);
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
                        stop = event_callback(val ? EVENT_KEY_PRESSED : EVENT_KEY_RELEASED, row * num_kb_cols + col);
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
}
int print_event(int ev_type, int value) {
    switch (ev_type) {
        case EVENT_ENCODER_MINUS:
        case EVENT_ENCODER_PLUS:
            printf("Encoder event: %s, value: %d\n", ev_type == EVENT_ENCODER_PLUS ? "↻" : "↺", value);
            break;
        case EVENT_JACK_DETECT:
            printf("Jack detect event: %d\n", value);
            break;
        case EVENT_KEY_PRESSED:
        case EVENT_KEY_RELEASED:
            printf("Key event: %s, key: %s\n", ev_type == EVENT_KEY_PRESSED ? "pressed" : "released", key_labels[value]);
            break;
        case EVENT_REMOTE_PRESSED:
        case EVENT_REMOTE_REPEAT:
            printf("Remote event: scancode=0x%x%s\n", value, ev_type == EVENT_REMOTE_REPEAT ? " (repeat)" : "");
            break;
    }
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
    ir_close();
}
