#ifndef JVC_CONTROL_H
#define JVC_CONTROL_H
#define EVENT_ENCODER_PLUS 1
#define EVENT_ENCODER_MINUS 2
#define EVENT_JACK_DETECT 3
#define EVENT_KEY_PRESSED 4
#define EVENT_KEY_RELEASED 5
#define EVENT_REMOTE_PRESSED 6
#define EVENT_REMOTE_REPEAT 7
#define JVC_KEY_TA_NEWS_INFO 0
#define JVC_KEY_EON_ON_OFF 1
#define JVC_KEY_DISPLAY_MODE 2
#define JVC_KEY_PTY_SEARCH 3
#define JVC_KEY_KEY_MODE 4
#define JVC_KEY_PREV 5
#define JVC_KEY_NEXT 6
#define JVC_KEY_INPUT 7
#define JVC_KEY_DIRECT 8
#define JVC_KEY_S_A_BASS 9
#define JVC_KEY_BAND 10
#define JVC_KEY_STANDBY 11
#define JVC_LED_S_A_BASS 0
#define JVC_LED_STANDBY 1
#define JVC_LED_DIRECT 2
#define JVC_REMOTE_KEY_POWER 0x0c
#define JVC_REMOTE_KEY_CH_UP 0x10
#define JVC_REMOTE_KEY_CH_DOWN 0x11
#define JVC_REMOTE_KEY_VOL_UP 0x20
#define JVC_REMOTE_KEY_VOL_DOWN 0x21
#define IR_TECHNICS_POWER 0xc7437e
#define IR_TECHNICS_VOL_UP 0xFFFBFE
#define IR_TECHNICS_VOL_DOWN 0xFF7B7E
#define IR_TECHNICS_INPUT_VDP 0xFFBABF
#define IR_TECHNICS_INPUT_DAT 0xFFE600

void control_event_loop(int (*event_callback)(int ev_type, int value));
void control_set_led(int led, int value);
void ir_tx_send(uint64_t code);
int print_event(int ev_type, int value);
#endif
