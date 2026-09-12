#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/timex.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "display.h"
#include "wlan_check.h"
#include "control.h"
#include "player/preview_shm.h"
SH1122* oled;
FrameBuffer* fb;
static volatile sig_atomic_t shutdown_requested = 0;
void display_show(void);

static int current_window_idx = 6;
#define max_window 7

void send_mpv_keypress(char key) {
    char msg[2] = { 'K', key };
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/.mpv.socket", sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        perror("connect");
    }
    write(fd, msg, 2);
    close(fd);
}
static int direct_flag, sa_bass_flag, standby_flag;
static int text_mode = 0, tv_on = 0;
static int control_event_callback(int ev_type, int value) {
    // print_event(ev_type, value);
    if (ev_type == EVENT_KEY_PRESSED) {
        if (value == JVC_KEY_DIRECT) control_set_led(JVC_LED_DIRECT, direct_flag = !direct_flag);
        if (value == JVC_KEY_S_A_BASS) control_set_led(JVC_LED_S_A_BASS, sa_bass_flag = !sa_bass_flag);
        if (value == JVC_KEY_STANDBY) control_set_led(JVC_LED_STANDBY, standby_flag = !standby_flag);
        if (value == JVC_KEY_PREV) current_window_idx = (current_window_idx + max_window - 1) % max_window;
        if (value == JVC_KEY_NEXT) current_window_idx = (current_window_idx + 1) % max_window;
        if (value == JVC_KEY_BAND) ir_tx_send(IR_TECHNICS_POWER);
        if (value == JVC_KEY_DISPLAY_MODE) ir_tx_send_tv(IR_THOMSON_AV);
    }
    if (ev_type == EVENT_REMOTE_PRESSED) {
        if (value == JVC_REMOTE_KEY_POWER) control_set_led(JVC_LED_STANDBY, standby_flag = !standby_flag);
        if (value == JVC_REMOTE_KEY_CH_DOWN) text_mode ? ir_tx_send_tv(IR_THOMSON_CH_DOWN) : send_mpv_keypress('s');
        if (value == JVC_REMOTE_KEY_CH_UP) text_mode ? ir_tx_send_tv(IR_THOMSON_CH_UP) : send_mpv_keypress('w');
        if (value >= JVC_REMOTE_KEY_0 && value <= JVC_REMOTE_KEY_9) {
            if (text_mode) {
                int code[10] = {IR_THOMSON_0, IR_THOMSON_1, IR_THOMSON_2, IR_THOMSON_3, IR_THOMSON_4, IR_THOMSON_5, IR_THOMSON_6, IR_THOMSON_7, IR_THOMSON_8, IR_THOMSON_9};
                ir_tx_send_tv(code[value - JVC_REMOTE_KEY_0]);
            } else send_mpv_keypress('0' + value - JVC_REMOTE_KEY_0);
        }
        if (value == JVC_REMOTE_KEY_RED && text_mode) ir_tx_send_tv(IR_THOMSON_RED);
        if (value == JVC_REMOTE_KEY_GREEN && text_mode) ir_tx_send_tv(IR_THOMSON_GREEN);
        if (value == JVC_REMOTE_KEY_YELLOW && text_mode) ir_tx_send_tv(IR_THOMSON_YELLOW);
        if (value == JVC_REMOTE_KEY_BLUE && text_mode) ir_tx_send_tv(IR_THOMSON_BLUE);
        if (value == JVC_REMOTE_KEY_OPTIONS && text_mode) ir_tx_send_tv(IR_THOMSON_MENU);
        if (value == JVC_REMOTE_KEY_EXIT) {
            ir_tx_send_tv(tv_on ? IR_THOMSON_POWER : IR_THOMSON_AV);
            tv_on = !tv_on;
            text_mode = 0;
        }
        if (value == JVC_REMOTE_KEY_FF) send_mpv_keypress('.');
        if (value == JVC_REMOTE_KEY_REW) send_mpv_keypress(',');
        if (value == JVC_REMOTE_KEY_PLAY) send_mpv_keypress(' ');
        if (value == JVC_REMOTE_KEY_PAUSE) send_mpv_keypress(' ');
        if (value == JVC_REMOTE_KEY_SUBTITLE) send_mpv_keypress('t');
        if (value == JVC_REMOTE_KEY_INFO) send_mpv_keypress('i');
        if (value == JVC_REMOTE_KEY_FORMAT) send_mpv_keypress('x');
        if (value == JVC_REMOTE_KEY_UP) send_mpv_keypress('d');
        if (value == JVC_REMOTE_KEY_DOWN) send_mpv_keypress('a');
        if (value == JVC_REMOTE_KEY_TVGUIDE) send_mpv_keypress('r');
        if (value == JVC_REMOTE_KEY_TEXT) {
            ir_tx_send_tv(text_mode ? IR_THOMSON_EXIT : IR_THOMSON_TEXT);
            text_mode = !text_mode;
        }
    }
    if (ev_type == EVENT_REMOTE_PRESSED || ev_type == EVENT_REMOTE_REPEAT) {
        if (value == JVC_REMOTE_KEY_RIGHT) send_mpv_keypress('>');
        if (value == JVC_REMOTE_KEY_LEFT) send_mpv_keypress('<');
        if (value == JVC_REMOTE_KEY_VOL_UP) ir_tx_send(IR_TECHNICS_VOL_UP);
        if (value == JVC_REMOTE_KEY_VOL_DOWN) ir_tx_send(IR_TECHNICS_VOL_DOWN);
    }
    if (ev_type == EVENT_ENCODER_PLUS) {
        ir_tx_send(IR_TECHNICS_INPUT_VDP);
        usleep(10000);
        ir_tx_send(IR_TECHNICS_VOL_UP);
    }
    if (ev_type == EVENT_ENCODER_MINUS) ir_tx_send(IR_TECHNICS_VOL_DOWN);
    return shutdown_requested;
}

static void *control_thread_main(void *arg) {
    (void)arg;
    control_event_loop(control_event_callback);
    shutdown_requested = 1;
    return NULL;
}

static void handle_shutdown_signal(int sig) {
    shutdown_requested = 1;
}

static void display_shutdown_screen(void) {
    if (!fb) return;

    framebuffer_fill(fb, 0);
    framebuffer_draw_text(fb, 16, 68, 30, "Shutting down...");
    display_show();
}

void display_init() {
    framebuffer_init();
    oled = sh1122_create("/dev/spidev0.0", 256, 48, 2);
    if (!oled) {
        fprintf(stderr, "Failed to initialize display\n");
        exit(1);
    }
    fb = oled->fb;
    
    printf("SH1122 OLED display driver loaded\n");
}
void display_close() {
    if (oled) {
        sh1122_destroy(oled);
        oled = NULL;
    }
}
void display_show() {
    if (oled) sh1122_show(oled);
}
// Main program
struct window_t {
    FrameBuffer* fb;
    int64_t last_update_time;
    void (*update_func)(struct window_t *w);
    int32_t update_frequency_s;
};

static int preview_fd = -1;
static const struct preview_shm_header *preview_header;
static const uint8_t *preview_pixels;

static void preview_shm_close_consumer(void) {
    if (preview_header) {
        munmap((void *)preview_header, sizeof(struct preview_shm_header) + PREVIEW_SHM_BYTES);
        preview_header = NULL;
        preview_pixels = NULL;
    }
    if (preview_fd != -1) {
        close(preview_fd);
        preview_fd = -1;
    }
}

static int preview_shm_open_consumer(void) {
    struct stat st;
    void *mapping;

    if (preview_header) return 0;

    preview_fd = shm_open(PREVIEW_SHM_NAME, O_RDONLY, 0);
    if (preview_fd == -1) return -1;
    if (fstat(preview_fd, &st) == -1 ||
        st.st_size < (off_t)(sizeof(struct preview_shm_header) + PREVIEW_SHM_BYTES)) {
        preview_shm_close_consumer();
        return -1;
    }
    mapping = mmap(NULL, sizeof(struct preview_shm_header) + PREVIEW_SHM_BYTES,
                   PROT_READ, MAP_SHARED, preview_fd, 0);
    if (mapping == MAP_FAILED) {
        preview_shm_close_consumer();
        return -1;
    }
    preview_header = mapping;
    preview_pixels = (const uint8_t *)mapping + sizeof(*preview_header);
    return 0;
}

void update_preview(struct window_t* w) {
    uint8_t pixels[PREVIEW_SHM_BYTES];
    uint32_t first_sequence;
    uint32_t last_sequence;
    static char metadata[768];
    static char *artist, *song_name, *year;

    framebuffer_fill(w->fb, 0);
    if (preview_shm_open_consumer() != 0) return;
    int position, duration;
    static char name[NAME_MAX+1];
    int new_name = 0;

    do {
        first_sequence = __atomic_load_n(&preview_header->sequence, __ATOMIC_ACQUIRE);
        if (first_sequence & 1) continue;
        memcpy(pixels, preview_pixels, sizeof(pixels));
        position = preview_header->position / 1000;
        duration = preview_header->duration / 1000;
        char *name0 = strrchr(preview_header->filename, '/');
        char *name1 = strrchr(preview_header->filename, '.');
        if (name0 && name1) {
            name0++;
            int len = name1 - name0;
            if (len != strlen(name) || memcmp(name0, name, len)) {
                memcpy(name, name0, len);
                name[len] = 0;
                new_name = 1;
            }
        }
        last_sequence = __atomic_load_n(&preview_header->sequence, __ATOMIC_ACQUIRE);
    } while (first_sequence != last_sequence || (last_sequence & 1));
    if (duration)
        framebuffer_draw_text_fmt(w->fb, 10, 194 - direct_flag * preview_header->width, 10, "%02d:%02d/%02d:%02d", position/60, position%60, duration/60, duration%60);
    if (new_name) {
        char command[PATH_MAX + 64];
        snprintf(command, sizeof(command), "jq --raw-output0 '.artist,.name,.year' \"/media/HDD/music videos/%s.meta.json\"", name);
        FILE *pipe = popen(command, "r");
        if (pipe) {
            size_t bytes_read = fread(metadata, 1, sizeof(metadata) - 1, pipe);
            metadata[bytes_read] = '\0';
            pclose(pipe);
            artist = metadata;
            song_name = artist + strlen(artist) + 1;
            year = song_name + strlen(song_name) + 1;
        }
    }
    font4_set_color(8);
    framebuffer_draw_text(w->fb, 12, 0, 22, artist);
    font4_set_color(15);
    framebuffer_draw_text(w->fb, 14, 0, 38, song_name);
    framebuffer_draw_text(w->fb, 10, 228 - direct_flag * preview_header->width, 47, year);

    if (direct_flag) for (int y = 0; y < preview_header->height; y++) {
        uint8_t *dest = w->fb->buffer + y * 128 + 128 - (preview_header->width + 1) / 2;
        const uint8_t *source = pixels + y * PREVIEW_SHM_WIDTH;
        for (int x = 0; x < preview_header->width; x += 2) {
            uint8_t v0 = source[x] >> 4;
            uint8_t v1 = source[x + 1] >> 4;
            if ((source[x] & 0x0f) > (rand() & 0x0f) && v0 < 15) v0++;
            if ((source[x + 1] & 0x0f) > (rand() & 0x0f) && v1 < 15) v1++;
            dest[x / 2] = (uint8_t)((v0 << 4) | v1);
        }
    }
}

void update_time(struct window_t* w) {
    framebuffer_fill(w->fb, 0);    
    time_t now = time(NULL);
    struct timex txc;
    txc.modes = 0;
    int status = ntp_adjtime(&txc);
    if (status == TIME_ERROR) {
        framebuffer_draw_text(w->fb, 16, 16, 24+8, "System starting...");
        return;
    }
    struct tm *tm_info = localtime(&now);
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M", tm_info);
    framebuffer_draw_text(w->fb, 16, tm_info->tm_min * 3 + 16, 24+8, time_str);
}
int read_file_content(const char* path, char* buffer, size_t buffer_size) {
    FILE* file = fopen(path, "r");
    if (!file) return -1;
    if (!fgets(buffer, buffer_size, file)) {
        fclose(file);
        return -1;
    }    
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    fclose(file);
    return 0;
}
void update_temp_and_fan(struct window_t* w) {
    char temp_buf[32];
    framebuffer_fill(w->fb, 0);
    int temperature = 0;
    if(!read_file_content("/sys/class/thermal/thermal_zone0/temp", temp_buf, sizeof(temp_buf)))
        temperature = atoi(temp_buf);
    int fan_speed = 0;
    if(!read_file_content("/sys/class/thermal/cooling_device0/cur_state", temp_buf, sizeof(temp_buf)))
        fan_speed = atoi(temp_buf);
    
    framebuffer_draw_icon(w->fb, 20, 10, (48-20)/2, FA_TEMPERATURE_HIGH);
    framebuffer_draw_text_fmt(w->fb, 20, 40, (48+20)/2, "%3.1f", temperature/1000.0);
    for (int i = 0; i < 4; i++) {
        font4_set_color(i < fan_speed ? 16 : 2);
        framebuffer_draw_icon(w->fb, 20, 138 + i * 24, (48-20)/2, FA_FAN);
    }
    font4_set_color(16);
}
int read_process_output(const char* command, char* buffer, size_t buffer_size) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return -1;
    if (!fgets(buffer, buffer_size, pipe)) {
        pclose(pipe);
        return -1;
    }
    pclose(pipe);
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    return 0;
}
int read_process_output_all(const char* command, char* buffer, size_t buffer_size) {
    FILE* pipe = popen(command, "r");
    if (!pipe || buffer_size == 0) return -1;
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, pipe);
    int command_status = pclose(pipe);
    buffer[bytes_read] = '\0';
    return command_status == 0 ? 0 : -1;
}
void update_jellyfin_status(struct window_t* w) {
    char status[512];
    char device_name[128];
    char item_name[256];
    const char* command = "wget -qO- --timeout=2 --header=\"X-Emby-Authorization: MediaBrowser Token=\\\"$(cat jellyfin-token.txt)\\\"\" http://localhost:8096/Sessions | jq --raw-output0 'map(select(.NowPlayingItem != null and (.NowPlayingItem.Name // \"\") != \"\")) | if length > 0 then .[0] else empty end | [(.DeviceName // \"\"), (.NowPlayingItem.Name // \"\"), (((.PlayState.PositionTicks // 0) / 10000000) | floor), (((.NowPlayingItem.RunTimeTicks // 0) / 10000000) | floor)] | @tsv'";

    framebuffer_fill(w->fb, 0);
    framebuffer_draw_icon(w->fb, 16, 0, (48-16)/2, FA_FILM);
    if (read_process_output_all(command, status, sizeof(status))) {
        framebuffer_draw_text(w->fb, 12, 24, 24+6, "Jellyfin unavailable");
        return;
    }

    char *saveptr = NULL;
    char *device = strtok_r(status, "\t", &saveptr);
    char *name = strtok_r(NULL, "\t", &saveptr);
    char *position = strtok_r(NULL, "\t", &saveptr);
    char *duration = strtok_r(NULL, "\t", &saveptr);
    if (!device || !name || !position || !duration) {
        framebuffer_draw_text(w->fb, 12, 24, 24+6, "No Jellyfin session");
        return;
    }

    snprintf(device_name, sizeof(device_name), "%.*s", (int)sizeof(device_name) - 1, device);
    snprintf(item_name, sizeof(item_name), "%.*s", (int)sizeof(item_name) - 1, name);

    long pos_seconds = strtol(position, NULL, 10);
    long total_seconds = strtol(duration, NULL, 10);
    long pos_h = pos_seconds / 3600;
    long pos_m = (pos_seconds % 3600) / 60;
    long pos_s = pos_seconds % 60;
    long total_h = total_seconds / 3600;
    long total_m = (total_seconds % 3600) / 60;
    long total_s = total_seconds % 60;

    framebuffer_draw_text_fmt(w->fb, 10, 24, 13, "%s", device_name);
    if (item_name[0]) {
        framebuffer_draw_text_fmt(w->fb, 18, 24, 36, "%s", item_name);
        framebuffer_draw_text_fmt(w->fb, 12, 155, 12, "%ld:%02ld:%02ld/%ld:%02ld:%02ld",
                                 pos_h, pos_m, pos_s, total_h, total_m, total_s);
    } else {
        font4_set_color(5);
        framebuffer_draw_text(w->fb, 18, 24, 36, "Nothing playing");
        font4_set_color(16);
    }
}
void update_transmission_status(struct window_t* w) {
    char response[4096];
    const char* command = "transmission-remote -n transmission:transmission -j -l 2>/dev/null | jq -r '.arguments.torrents[] | select(.isFinished != true) | [(.leftUntilDone // 0), (.sizeWhenDone // 0), (.rateDownload / 1000), (.name // \"\")] | @tsv'";
    int active_count = 0;
    char* save_line = NULL;

    framebuffer_fill(w->fb, 0);
    framebuffer_draw_icon(w->fb, 16, 0, 16, FA_DOWNLOAD);
    if (read_process_output_all(command, response, sizeof(response))) {
        framebuffer_draw_text(w->fb, 12, 24, 24-6, "Transmission unavailable");
        return;
    }

    char* line = strtok_r(response, "\n", &save_line);
    while (line) {
        char* line_save = NULL;
        char* left_until_done = strtok_r(line, "\t", &line_save);
        char* size_when_done = strtok_r(NULL, "\t", &line_save);
        char* download_rate = strtok_r(NULL, "\t", &line_save);
        char* name = strtok_r(NULL, "\t", &line_save);

        if (left_until_done && size_when_done && download_rate && name) {
            double remaining = strtod(left_until_done, NULL);
            double total = strtod(size_when_done, NULL);
            int percent_done = total > 0.0 ? (int)((1.0 - (remaining / total)) * 100.0 + 0.5) : 0;
            if (active_count < 2) {
                framebuffer_draw_text_fmt(w->fb, 12, 24, 14 + active_count * 18,
                                          "%d%% %.0fkB/s %s", percent_done, strtod(download_rate, NULL), name);
            }
            active_count++;
        }
        line = strtok_r(NULL, "\n", &save_line);
    }
    if (!active_count) {
        framebuffer_draw_text(w->fb, 12, 24, 24+6, "No active tasks");
        return;
    }
    if (active_count > 2)
        framebuffer_draw_text_fmt(w->fb, 10, 184, 42, "+%d", active_count - 2);
}
void update_power_usage_monitor(struct window_t* w) {
    framebuffer_fill(w->fb, 0);
    char power_buf[32];
    char *(commands[5]) = {
#include "power_commands.h"
    };
    framebuffer_fill(w->fb, 0);
    framebuffer_draw_icon(w->fb, 16, 0, (48-16)/2, FA_PLUG);
    for (int i = 0; i < 5; i++) {
        if (!read_process_output(commands[i], power_buf, sizeof(power_buf))) {
            float usage = strtof(power_buf, 0);
            framebuffer_draw_text_fmt(w->fb, 12, i * 50 + 20, (48+12)/2, usage < 10 ? "%.1fW" : "%.0fW", usage);
        } else {
            framebuffer_draw_text(w->fb, 12, i * 50 + 20, (48+12)/2, "N/A");
        }
    }
}
void update_wifi_status(struct window_t* w) {
    framebuffer_fill(w->fb, 0);
    int signal = 0, rxrate = 0, txrate = 0;
    if (wlan_status("wlan1", &signal, &rxrate, &txrate) == EXIT_SUCCESS) {
        framebuffer_draw_icon(w->fb, 16, 0, (48-16)/2, FA_WIFI);
        if (signal) framebuffer_draw_text_fmt(w->fb, 16, 24, (48+16)/2, "%d dBm", signal);
        framebuffer_draw_text_fmt(w->fb, 12, 128, (48+12-12)/2, "RX: %.1f Mbit/s", (double)rxrate / 10.0);
        framebuffer_draw_text_fmt(w->fb, 12, 128, (48+12+12)/2, "TX: %.1f Mbit/s", (double)txrate / 10.0);
    } else {
        framebuffer_draw_text(w->fb, 12, 20, (48+12)/2, "WiFi N/A");
    }
}
struct window_t  windows[max_window] = {{
    .update_func = update_time,
    .update_frequency_s = 1
}, {
    .update_func = update_temp_and_fan,
    .update_frequency_s = 1
}, {
    .update_func = update_power_usage_monitor,
    .update_frequency_s = 2
}, {
    .update_func = update_wifi_status,
    .update_frequency_s = 1
}, {
    .update_func = update_jellyfin_status,
    .update_frequency_s = 5
}, {
    .update_func = update_transmission_status,
    .update_frequency_s = 5
}, {
    .update_func = update_preview,
    .update_frequency_s = 0
}};
void windows_init() {
    for (int i = 0; i < sizeof(windows)/sizeof(windows[0]); i++) {
        windows[i].fb = framebuffer_create(256, 48);
        windows[i].last_update_time = 0;
    }
}
void update_window(struct window_t* w) {
    int64_t current_time = time(NULL);
    int64_t elapsed_time = (current_time - w->last_update_time);
    if (w->last_update_time == 0 || elapsed_time >= w->update_frequency_s) {
        w->update_func(w);
        w->last_update_time = current_time;
    }
}
int main(int argc, char *argv[]) {
    pthread_t control_thread;
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);
    display_init();
    windows_init();

    if (pthread_create(&control_thread, NULL, control_thread_main, NULL) != 0) {
        fprintf(stderr, "Failed to start control thread\n");
        display_close();
        return 1;
    }

    if (argc > 1) {
        const char* filename = argv[1];
        printf("Watching PNG file: %s (reloading at 50fps)\n", filename);
        while (!shutdown_requested) {
            FrameBuffer *img_fb = framebuffer_create_from_png(filename);
            if (img_fb) {
                framebuffer_blit(fb, img_fb, (256 - img_fb->width) / 2, (48 - img_fb->height) / 2);
                display_show();
                framebuffer_destroy(img_fb);
            }
            usleep(20000);  // 50 fps (20ms per frame)
        }
    } else {
    // Create test image buffer (32x40 pixels)
    uint8_t image_data[32 * 20] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x11, 0x45, 0x56, 0x41, 0x00, 0x00, 0x00, 0x00, 0x14, 0x55, 0x54, 0x11, 0x00, 0x00,
        0x00, 0x16, 0x8A, 0xAA, 0xAA, 0xAA, 0x73, 0x00, 0x00, 0x36, 0x9A, 0xAA, 0xAA, 0xA9, 0x61, 0x00,
        0x00, 0x18, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x30, 0x02, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x91, 0x00,
        0x00, 0x18, 0xAA, 0x97, 0x8A, 0xAA, 0xAA, 0x80, 0x07, 0xAA, 0xAA, 0xA8, 0x79, 0xAA, 0x91, 0x00,
        0x00, 0x07, 0xAA, 0xAA, 0x85, 0x7A, 0xAA, 0xA1, 0x0A, 0xAA, 0xA8, 0x58, 0xAA, 0xAA, 0x70, 0x00,
        0x00, 0x03, 0xAA, 0xAA, 0xAA, 0x63, 0x9A, 0xA1, 0x0A, 0xA9, 0x45, 0xAA, 0xAA, 0xAA, 0x30, 0x00,
        0x00, 0x00, 0x6A, 0xAA, 0xAA, 0xA8, 0x27, 0x40, 0x03, 0x72, 0x8A, 0xAA, 0xAA, 0xA7, 0x00, 0x00,
        0x00, 0x00, 0x28, 0xAA, 0xAA, 0xAA, 0x91, 0x00, 0x00, 0x08, 0xAA, 0xAA, 0xAA, 0x83, 0x00, 0x00,
        0x00, 0x00, 0x03, 0x8A, 0xAA, 0xAA, 0x70, 0x00, 0x00, 0x06, 0xAA, 0xAA, 0xA8, 0x30, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x15, 0x8A, 0x95, 0x00, 0x00, 0x00, 0x00, 0x59, 0xA9, 0x52, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x34, 0x43, 0x10, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02, 0x55, 0x51, 0x26, 0x66, 0x66, 0x62, 0x05, 0x54, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x46, 0x66, 0x30, 0x66, 0x66, 0x66, 0x66, 0x12, 0x66, 0x65, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x03, 0x66, 0x63, 0x00, 0x46, 0x66, 0x66, 0x66, 0x10, 0x26, 0x66, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x06, 0x66, 0x20, 0x00, 0x03, 0x56, 0x66, 0x41, 0x00, 0x02, 0x66, 0x61, 0x00, 0x00,
        0x00, 0x00, 0x16, 0x51, 0x01, 0x23, 0x20, 0x00, 0x00, 0x13, 0x54, 0x20, 0x15, 0x62, 0x00, 0x00,
        0x00, 0x00, 0x13, 0x00, 0x36, 0x66, 0x65, 0x10, 0x01, 0x66, 0x66, 0x65, 0x00, 0x32, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02, 0x66, 0x66, 0x66, 0x50, 0x04, 0x66, 0x66, 0x66, 0x40, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x40, 0x06, 0x66, 0x66, 0x66, 0x61, 0x06, 0x66, 0x66, 0x66, 0x62, 0x05, 0x40, 0x00,
        0x00, 0x36, 0x60, 0x26, 0x66, 0x66, 0x66, 0x61, 0x06, 0x66, 0x66, 0x66, 0x64, 0x06, 0x64, 0x00,
        0x00, 0x66, 0x61, 0x36, 0x66, 0x66, 0x66, 0x60, 0x05, 0x66, 0x66, 0x66, 0x64, 0x16, 0x66, 0x10,
        0x02, 0x66, 0x60, 0x36, 0x66, 0x66, 0x66, 0x40, 0x02, 0x66, 0x66, 0x66, 0x64, 0x06, 0x66, 0x20,
        0x02, 0x66, 0x50, 0x16, 0x66, 0x66, 0x66, 0x10, 0x00, 0x46, 0x66, 0x66, 0x61, 0x05, 0x66, 0x20,
        0x01, 0x66, 0x40, 0x03, 0x66, 0x66, 0x51, 0x01, 0x10, 0x04, 0x66, 0x66, 0x30, 0x04, 0x66, 0x10,
        0x00, 0x56, 0x20, 0x00, 0x13, 0x32, 0x03, 0x66, 0x66, 0x30, 0x12, 0x21, 0x00, 0x02, 0x65, 0x00,
        0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x46, 0x66, 0x66, 0x65, 0x00, 0x00, 0x02, 0x31, 0x31, 0x00,
        0x00, 0x00, 0x36, 0x52, 0x00, 0x01, 0x66, 0x66, 0x66, 0x66, 0x20, 0x01, 0x56, 0x64, 0x00, 0x00,
        0x00, 0x00, 0x66, 0x66, 0x30, 0x03, 0x66, 0x66, 0x66, 0x66, 0x40, 0x15, 0x66, 0x66, 0x00, 0x00,
        0x00, 0x00, 0x66, 0x66, 0x63, 0x03, 0x66, 0x66, 0x66, 0x66, 0x40, 0x56, 0x66, 0x66, 0x00, 0x00,
        0x00, 0x00, 0x56, 0x66, 0x66, 0x11, 0x66, 0x66, 0x66, 0x66, 0x22, 0x66, 0x66, 0x65, 0x00, 0x00,
        0x00, 0x00, 0x36, 0x66, 0x66, 0x30, 0x46, 0x66, 0x66, 0x65, 0x04, 0x66, 0x66, 0x64, 0x00, 0x00,
        0x00, 0x00, 0x05, 0x66, 0x66, 0x40, 0x03, 0x66, 0x66, 0x30, 0x05, 0x66, 0x66, 0x51, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x56, 0x66, 0x40, 0x00, 0x01, 0x10, 0x00, 0x05, 0x66, 0x65, 0x10, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02, 0x33, 0x10, 0x01, 0x23, 0x32, 0x10, 0x02, 0x44, 0x30, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x66, 0x66, 0x65, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x66, 0x66, 0x66, 0x66, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x66, 0x66, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x43, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    
    // Create source framebuffer from image data
    FrameBuffer* fb_logo = framebuffer_create_with_buffer(32, 40, image_data);
    
    // Animation loop
    for (int step = 0; !shutdown_requested; step++) {
        struct window_t* current_window = &windows[current_window_idx];
        update_window(current_window);
        framebuffer_blit(fb, current_window->fb, 0, 0);
        // framebuffer_fill(fb, 0);
        // framebuffer_blit(fb, fb_logo, step - 32, 0);
        
        // // Draw animated rectangles at the bottom
        // for (int i = 0; i < 16; i++) {
        //     framebuffer_fill_rect(fb, i * 8, 55 - 16, 8, 8, (15 - i + step) % 16);
        //     framebuffer_fill_rect(fb, 247 - (i * 8), 55 - 16, 8, 8, (15 - i + step) % 16);
        // }
        // framebuffer_rect(fb, 0, 0, 256, 48, 15);
        // time_t now = time(NULL);
        // struct tm *local_now = localtime(&now);
        // framebuffer_draw_text_fmt(fb, 12, 2, 22, "Depeche Mode %02d:%02d", local_now->tm_hour, local_now->tm_min);
        // framebuffer_draw_text_fmt(fb, 10, 2, 32, "Enjoy the Silencę");
        
        // framebuffer_draw_icon(fb, 16, 160, 0, FA_WIFI);
        // framebuffer_draw_icon(fb, 16, 190, 0, FA_VOLUME_UP);
        // framebuffer_draw_icon(fb, 16, 220, 0, FA_TEMPERATURE_HIGH);
        display_show();
        usleep(1000);  // 1ms delay
    }
    
    // Clear display at the end
    // framebuffer_fill(fb, 0);
    // display_show();
    if (shutdown_requested) display_shutdown_screen();

    }
    preview_shm_close_consumer();
    display_close();
    return 0;
}
