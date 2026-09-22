#ifndef CDPLAYER_H
#define CDPLAYER_H
#define CDPLAYER_REPEAT_OFF 0
#define CDPLAYER_REPEAT_ONE 1
#define CDPLAYER_REPEAT_ALL 2
#define CDPLAYER_REPEAT_SHUFFLE_ALL 3
#define CDPLAYER_REPEAT_NUM_MODES 4
void cdplayer_load_media(char *dir);
void cdplayer_save(void);
void cdplayer_close(void);
void cdplayer_pause(void);
void cdplayer_play(void);
void cdplayer_stop(void);
void cdplayer_set_track(int n);
void cdplayer_seek_s(int delta_s);
void cdplayer_set_repeat(int mode);
int cdplayer_get_repeat(void);
int cdplayer_get_position_s(void);
int cdplayer_get_duration_s(void);
int cdplayer_get_track_nr(void);
int cdplayer_is_playing(void);
int cdplayer_get_track_count(void);
char *cdplayer_get_artist(void);
char *cdplayer_get_song_title(void);
char *cdplayer_get_album_title(void);
char *cdplayer_get_cd_dat_path(void);
#endif