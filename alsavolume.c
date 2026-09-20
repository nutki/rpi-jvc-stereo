#include <alsa/asoundlib.h>
#include <stdio.h>
#include <math.h>

static snd_mixer_t *mixer;
static snd_mixer_elem_t *pcm_elem;
static long db_min;
static long db_max;

static double percent_to_db(int percent) {
    double p = percent / 100.0;
    double min = pow(10.0, (db_min - db_max) / 6000.0);
    double v = min + p * (1.0 - min);
    double res = db_max + 6000.0 * log10(v); 
    return res;
}

int alsa_volume_init(void) {
    snd_mixer_selem_id_t *sid;
    if (snd_mixer_open(&mixer, 0) < 0) return -1;
    if (snd_mixer_attach(mixer, "hw:CARD=CODEC") < 0) goto error;
    if (snd_mixer_selem_register(mixer, NULL, NULL) < 0) goto error;
    if (snd_mixer_load(mixer) < 0) goto error;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_name(sid, "PCM");
    pcm_elem = snd_mixer_find_selem(mixer, sid);
    if (!pcm_elem) goto error;
    snd_mixer_selem_get_playback_dB_range(pcm_elem, &db_min, &db_max);
    return 0;
error:
    snd_mixer_close(mixer);
    mixer = NULL;
    return -1;
}

int alsa_volume_set(int percent) {
    if (!pcm_elem) {
        alsa_volume_init();
        if (!pcm_elem) return -1;
    }
    long db = percent_to_db(percent);
    return snd_mixer_selem_set_playback_dB_all(pcm_elem, db, 0);
}
