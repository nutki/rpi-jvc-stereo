#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include "cdplayer.h"

#include <vlc/vlc.h>

static char cdplayer_dir[PATH_MAX];
static char **cdplayer_tracks = NULL;
static size_t cdplayer_track_count = 0;
static size_t cdplayer_current_track = 0;
static int cdplayer_repeat = CDPLAYER_REPEAT_OFF;

static libvlc_instance_t *cdplayer_vlc_instance = NULL;
static libvlc_media_player_t *cdplayer_vlc_player = NULL;
static libvlc_event_manager_t *cdplayer_vlc_events = NULL;

static pthread_t cdplayer_worker_thread;
static pthread_mutex_t cdplayer_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cdplayer_event_cond = PTHREAD_COND_INITIALIZER;
static int cdplayer_event_pending = 0;
static int cdplayer_worker_exit = 0;

static void cdplayer_handle_media_ended(void) {
    if (cdplayer_track_count == 0) return;

    if (cdplayer_repeat == CDPLAYER_REPEAT_ONE) {
        cdplayer_set_track((int)cdplayer_current_track);
        cdplayer_play();
        return;
    }

    if (cdplayer_repeat == CDPLAYER_REPEAT_SHUFFLE_ALL) {
        size_t next = cdplayer_current_track;
        if (cdplayer_track_count > 1) {
            do {
                next = (size_t)(rand() % cdplayer_track_count);
            } while (next == cdplayer_current_track);
        }
        cdplayer_current_track = next;
        cdplayer_set_track((int)cdplayer_current_track);
        cdplayer_play();
        return;
    }

    if (cdplayer_repeat == CDPLAYER_REPEAT_ALL) {
        size_t next = (cdplayer_current_track + 1) % cdplayer_track_count;
        cdplayer_current_track = next;
        cdplayer_set_track((int)cdplayer_current_track);
        cdplayer_play();
        return;
    }

    if (cdplayer_current_track + 1 < cdplayer_track_count) {
        cdplayer_current_track++;
        cdplayer_set_track((int)cdplayer_current_track);
        cdplayer_play();
        return;
    }

    cdplayer_stop();
}

static void *cdplayer_worker_loop(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&cdplayer_event_lock);
        while (!cdplayer_event_pending && !cdplayer_worker_exit) {
            pthread_cond_wait(&cdplayer_event_cond, &cdplayer_event_lock);
        }
        int pending = cdplayer_event_pending;
        cdplayer_event_pending = 0;
        pthread_mutex_unlock(&cdplayer_event_lock);

        if (cdplayer_worker_exit) break;
        if (pending) cdplayer_handle_media_ended();
    }
    return NULL;
}

static void cdplayer_media_ended_callback(const libvlc_event_t *event, void *data) {
    (void)event;
    (void)data;

    pthread_mutex_lock(&cdplayer_event_lock);
    cdplayer_event_pending = 1;
    pthread_cond_signal(&cdplayer_event_cond);
    pthread_mutex_unlock(&cdplayer_event_lock);
}

static int cdplayer_is_audio_file(const char *name) {
    if (!name || !name[0]) return 0;
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    dot++;
    static const char *extensions[] = {
        "mp3", "flac", "wav", "ogg", "oga", "m4a", "aac", "opus", "wma",
        "mid", "midi", "aiff", "ape", "alac"
    };
    size_t ext_count = sizeof(extensions) / sizeof(extensions[0]);
    for (size_t i = 0; i < ext_count; i++) {
        if (strcasecmp(dot, extensions[i]) == 0) return 1;
    }
    return 0;
}

static int cdplayer_compare_names(const void *a, const void *b) {
    const char *const *left = (const char *const *)a;
    const char *const *right = (const char *const *)b;
    return strcasecmp(*left, *right);
}

static void cdplayer_free_tracks(void) {
    if (!cdplayer_tracks) return;
    for (size_t i = 0; i < cdplayer_track_count; i++) {
        free(cdplayer_tracks[i]);
    }
    free(cdplayer_tracks);
    cdplayer_tracks = NULL;
    cdplayer_track_count = 0;
    cdplayer_current_track = 0;
}

static libvlc_media_player_t *cdplayer_ensure_vlc(void) {
    if (!cdplayer_vlc_instance) {
        static const char *vlc_args[] = {
            "--aout=alsa",  "--alsa-audio-device=hw:CARD=CODEC",
            "--intf=none",
            "--no-video-title-show",
            "--quiet",
            "--ignore-config",
            "--no-media-library"
        };
        cdplayer_vlc_instance = libvlc_new(sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);
    }
    if (!cdplayer_vlc_instance) return 0;
    if (!cdplayer_vlc_player) {
        cdplayer_vlc_player = libvlc_media_player_new(cdplayer_vlc_instance);
        cdplayer_vlc_events = libvlc_media_player_event_manager(cdplayer_vlc_player);
        if (cdplayer_vlc_events) {
            libvlc_event_attach(cdplayer_vlc_events, libvlc_MediaPlayerEndReached,
                                cdplayer_media_ended_callback, NULL);
        }
        if (pthread_create(&cdplayer_worker_thread, NULL, cdplayer_worker_loop, NULL) != 0) {
            cdplayer_worker_thread = 0;
        }
    }
    return cdplayer_vlc_player;
}

static int cdplayer_build_path(char *buf, size_t buf_size, const char *name) {
    if (!buf || buf_size == 0 || !name) return -1;
    if (cdplayer_dir[0] == '\0') return -1;
    int used = snprintf(buf, buf_size, "%s/%s", cdplayer_dir, name);
    if (used < 0 || (size_t)used >= buf_size) return -1;
    return 0;
}

static int cdplayer_build_state_path(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0 || cdplayer_dir[0] == '\0') return -1;
    int used = snprintf(buf, buf_size, "%s/player.ini", cdplayer_dir);
    if (used < 0 || (size_t)used >= buf_size) return -1;
    return 0;
}

static void cdplayer_set_path_state(const char *path, int start_pos_s) {
    if (!path || !path[0]) return;
    if (cdplayer_ensure_vlc()) {
        libvlc_media_t *media = libvlc_media_new_path(cdplayer_vlc_instance, path);
        if (media) {
            if (start_pos_s) {
                char start_option[32];
                snprintf(start_option, sizeof(start_option), ":start-time=%d", start_pos_s);
                libvlc_media_add_option(media, start_option);
            }
            libvlc_media_player_set_media(cdplayer_vlc_player, media);
            libvlc_media_release(media);
        }
    }
}

static void cdplayer_load_state_file(int *initial_position) {
    if (!cdplayer_dir[0]) return;

    char path[PATH_MAX];
    if (cdplayer_build_state_path(path, sizeof(path)) != 0) return;
    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char *trim = line + strspn(line, " \t\r\n");
        if (*trim == '\0' || *trim == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        while (*value == ' ' || *value == '\t') value++;

        if (strcasecmp(key, "track") == 0) {
            long index = strtol(value, NULL, 10);
            if (index > 0) cdplayer_current_track = (size_t)(index - 1);
        } else if (strcasecmp(key, "position") == 0) {
            *initial_position = (int)strtol(value, NULL, 10);
        } else if (strcasecmp(key, "repeat") == 0) {
            cdplayer_repeat = (int)strtol(value, NULL, 10);
        }
    }

    fclose(file);
}

static void cdplayer_save_state_file(void) {
    if (!cdplayer_dir[0]) return;
    char path[PATH_MAX];
    if (cdplayer_build_state_path(path, sizeof(path)) != 0) return;
    FILE *file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "track=%zu\n", cdplayer_track_count ? cdplayer_current_track + 1 : 0);
    fprintf(file, "position=%d\n", cdplayer_get_position_s());
    fprintf(file, "repeat=%d\n", cdplayer_repeat);
    fclose(file);
}


void cdplayer_load_media(char *dir) {
    if (!dir || !dir[0]) return;
    if (!strcmp(cdplayer_dir, dir) && cdplayer_is_playing()) return;

    if (cdplayer_is_playing()) cdplayer_save();
    cdplayer_free_tracks();
    cdplayer_repeat = CDPLAYER_REPEAT_OFF;
    snprintf(cdplayer_dir, sizeof(cdplayer_dir), "%s", dir);

    DIR *directory = opendir(cdplayer_dir);
    if (!directory) {
        cdplayer_dir[0] = '\0';
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!cdplayer_is_audio_file(entry->d_name)) continue;
        cdplayer_tracks = realloc(cdplayer_tracks, sizeof(char *) * (cdplayer_track_count + 1));
        if (!cdplayer_tracks) {
            closedir(directory);
            cdplayer_free_tracks();
            return;
        }
        cdplayer_tracks[cdplayer_track_count] = strdup(entry->d_name);
        if (!cdplayer_tracks[cdplayer_track_count]) {
            closedir(directory);
            cdplayer_free_tracks();
            return;
        }
        cdplayer_track_count++;
    }
    closedir(directory);

    if (cdplayer_track_count > 1) {
        qsort(cdplayer_tracks, cdplayer_track_count, sizeof(char *), cdplayer_compare_names);
    }
    int initial_position = 0;
    cdplayer_load_state_file(&initial_position);
    if (cdplayer_track_count > 0) {
        if (cdplayer_current_track >= cdplayer_track_count) {
            cdplayer_current_track = 0;
        }
        char path[PATH_MAX];
        if (cdplayer_build_path(path, sizeof(path), cdplayer_tracks[cdplayer_current_track]) == 0) {
            cdplayer_set_path_state(path, initial_position);
        }
        libvlc_media_player_play(cdplayer_vlc_player);
    }
}

void cdplayer_save() {
    cdplayer_save_state_file();
}

void cdplayer_close() {
    cdplayer_worker_exit = 1;
    pthread_mutex_lock(&cdplayer_event_lock);
    cdplayer_event_pending = 1;
    pthread_cond_signal(&cdplayer_event_cond);
    pthread_mutex_unlock(&cdplayer_event_lock);
    if (cdplayer_worker_thread) {
        pthread_join(cdplayer_worker_thread, NULL);
        cdplayer_worker_thread = 0;
    }
    cdplayer_worker_exit = 0;
    cdplayer_event_pending = 0;

    if (cdplayer_vlc_player) {
        libvlc_media_player_stop(cdplayer_vlc_player);
        libvlc_media_player_release(cdplayer_vlc_player);
        cdplayer_vlc_player = NULL;
    }
    if (cdplayer_vlc_instance) {
        libvlc_release(cdplayer_vlc_instance);
        cdplayer_vlc_instance = NULL;
    }
    cdplayer_free_tracks();
    cdplayer_dir[0] = '\0';
}

void cdplayer_pause() {
    if (!cdplayer_track_count) return;
    if (!cdplayer_ensure_vlc()) return;
    libvlc_media_player_pause(cdplayer_vlc_player);
}

void cdplayer_play() {
    if (!cdplayer_track_count) return;
    if (cdplayer_current_track >= cdplayer_track_count) {
        cdplayer_current_track = 0;
    }
    if (!cdplayer_ensure_vlc()) return;
    libvlc_state_t state = libvlc_media_player_get_state(cdplayer_vlc_player);
    if (state == libvlc_Paused) {
        libvlc_media_player_pause(cdplayer_vlc_player);
    } else if (state != libvlc_Playing) {
        char path[PATH_MAX];
        if (cdplayer_build_path(path, sizeof(path), cdplayer_tracks[cdplayer_current_track]) == 0) {
            cdplayer_set_path_state(path, 0);
        }
        libvlc_media_player_play(cdplayer_vlc_player);
    }
}

void cdplayer_stop() {
    if (cdplayer_ensure_vlc()) libvlc_media_player_stop(cdplayer_vlc_player);
    cdplayer_current_track = 0;
}

void cdplayer_set_track(int n) { // set track no
    if (!cdplayer_track_count) return;
    if (n < 0) n = 0;
    if ((size_t)n >= cdplayer_track_count) n = (int)cdplayer_track_count - 1;
    cdplayer_current_track = (size_t)n;

    char path[PATH_MAX];
    if (cdplayer_build_path(path, sizeof(path), cdplayer_tracks[cdplayer_current_track]) == 0) {
        cdplayer_set_path_state(path, 0);
    }

    if (cdplayer_ensure_vlc()) libvlc_media_player_play(cdplayer_vlc_player);
}

void cdplayer_seek_s(int delta_s) { // seek in the current track
    if (!cdplayer_track_count) return;
    int pos = cdplayer_get_position_s();
    int dur = cdplayer_get_duration_s();
    int new_pos = pos + delta_s;
    if (new_pos < 0) new_pos = 0;
    if (dur > 0 && new_pos > dur) new_pos = dur;

    cdplayer_ensure_vlc();
    if (cdplayer_vlc_player) {
        libvlc_media_player_set_time(cdplayer_vlc_player, (libvlc_time_t)new_pos * 1000LL);
    }
}

void cdplayer_set_repeat(int mode) {
    if (mode < 0) {
        mode = (cdplayer_repeat + 1) % CDPLAYER_REPEAT_NUM_MODES;
    }
    if (mode >= CDPLAYER_REPEAT_NUM_MODES) mode = CDPLAYER_REPEAT_OFF;
    cdplayer_repeat = mode;
}

int cdplayer_get_repeat() {
    return cdplayer_repeat;
}

int cdplayer_get_position_s() {
    if (!cdplayer_track_count || !cdplayer_ensure_vlc()) return 0;
    return (int)(libvlc_media_player_get_time(cdplayer_vlc_player) / 1000LL);
}

int cdplayer_get_duration_s() {
    if (!cdplayer_track_count || !cdplayer_ensure_vlc()) return 0;
    return (int)(libvlc_media_player_get_length(cdplayer_vlc_player) / 1000LL);
}

int cdplayer_get_track_nr() {
    return cdplayer_track_count ? (int)cdplayer_current_track + 1 : 0;
}

int cdplayer_is_playing() {
    if (!cdplayer_track_count) return 0;
    if (!cdplayer_vlc_instance) return 0;
    return libvlc_media_player_get_state(cdplayer_vlc_player) == libvlc_Playing;
}

int cdplayer_get_track_count() {
    return (int)cdplayer_track_count;
}

char *cdplayer_get_artist() {
    static char artist[256] = {0};
    if (!cdplayer_vlc_player) return artist;
    libvlc_media_t *media = libvlc_media_player_get_media(cdplayer_vlc_player);
    if (!media) return artist;
    const char *meta = libvlc_media_get_meta(media, libvlc_meta_Artist);
    if (!meta || !meta[0]) {
        artist[0] = '\0';
        return artist;
    }
    snprintf(artist, sizeof(artist), "%s", meta);
    return artist;
}

char *cdplayer_get_song_title() {
    static char title[256] = {0};
    if (!cdplayer_vlc_player) return title;
    libvlc_media_t *media = libvlc_media_player_get_media(cdplayer_vlc_player);
    if (!media) return title;
    const char *meta = libvlc_media_get_meta(media, libvlc_meta_Title);
    if (!meta || !meta[0]) {
        title[0] = '\0';
        return title;
    }
    snprintf(title, sizeof(title), "%s", meta);
    return title;
}

char *cdplayer_get_album_title() {
    static char album[256] = {0};
    if (!cdplayer_vlc_player) return album;
    libvlc_media_t *media = libvlc_media_player_get_media(cdplayer_vlc_player);
    if (!media) return album;
    const char *meta = libvlc_media_get_meta(media, libvlc_meta_Album);
    if (!meta || !meta[0]) {
        album[0] = '\0';
        return album;
    }
    snprintf(album, sizeof(album), "%s", meta);
    return album;
}
#define VLC_FOURCC(a,b,c,d) (((uint32_t)(a)) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define VLC_CODEC_MP4A VLC_FOURCC('m','p','4','a')
#define VLC_CODEC_MPGA VLC_FOURCC('m','p','g','a')
#define VLC_CODEC_FLAC VLC_FOURCC('f','l','a','c')
char *cdplayer_get_codec() {
    if (!cdplayer_vlc_player) return "";
    libvlc_media_t *media = libvlc_media_player_get_media(cdplayer_vlc_player);
    if (!media) return "";
    int current_id = libvlc_audio_get_track(cdplayer_vlc_player);
    libvlc_media_track_t **tracks;
    unsigned count = libvlc_media_tracks_get(media, &tracks);
    for (unsigned i = 0; i < count; ++i) {
        libvlc_media_track_t *track = tracks[i];
        if (track->i_type == libvlc_track_audio && track->i_id == current_id) {
            if (track->i_codec == VLC_CODEC_FLAC) return "FLAC";
            else if (track->i_codec == VLC_CODEC_MP4A) return "AAC";
            else if (track->i_codec == VLC_CODEC_MPGA) {
                static char codec[16] = {0};
                snprintf(codec, sizeof codec, "MP3 %d", track->i_bitrate/1000);
                return codec;
            }
            return "Unknown";
        }
    }
    return "";
}
