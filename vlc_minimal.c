// Minimal libvlc video player
// Compile: gcc -o vlc_minimal vlc_minimal.c -lvlc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlc/vlc.h>

// Event handler for media player events
static void event_handler(const libvlc_event_t *event, void *data) {
    int *playing = (int *)data;
    
    switch (event->type) {
        case libvlc_MediaPlayerPlaying:
            printf("Event: Playing\n");
            *playing = 1;
            break;
        case libvlc_MediaPlayerEndReached:
            printf("Event: End reached\n");
            *playing = 0;
            break;
        case libvlc_MediaPlayerStopped:
            printf("Event: Stopped\n");
            *playing = 0;
            break;
        case libvlc_MediaPlayerEncounteredError:
            printf("Event: Error\n");
            *playing = -1;
            break;
        default:
            break;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <video_file>\n", argv[0]);
        return 1;
    }

    // Create VLC instance with default settings
    libvlc_instance_t *vlc = libvlc_new(0, NULL);
    if (!vlc) {
        fprintf(stderr, "Failed to create VLC instance\n");
        return 1;
    }

    // Create media from file path
    libvlc_media_t *media = libvlc_media_new_path(vlc, argv[1]);
    if (!media) {
        fprintf(stderr, "Failed to load media: %s\n", argv[1]);
        libvlc_release(vlc);
        return 1;
    }

    // Create media player
    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);
    
    if (!player) {
        fprintf(stderr, "Failed to create media player\n");
        libvlc_release(vlc);
        return 1;
    }

    // Set up event handling
    int playing = 0;
    libvlc_event_manager_t *em = libvlc_media_player_event_manager(player);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, event_handler, &playing);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, event_handler, &playing);
    libvlc_event_attach(em, libvlc_MediaPlayerStopped, event_handler, &playing);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, event_handler, &playing);

    // Play
    printf("Playing: %s\n", argv[1]);
    int ret = libvlc_media_player_play(player);
    if (ret != 0) {
        fprintf(stderr, "Failed to start playback\n");
        libvlc_media_player_release(player);
        libvlc_release(vlc);
        return 1;
    }

    // Wait for playback to start and finish
    printf("Waiting for playback...\n");
    while (playing == 0) {
        sleep(1);
    }
    
    if (playing < 0) {
        fprintf(stderr, "Playback error\n");
        libvlc_media_player_release(player);
        libvlc_release(vlc);
        return 1;
    }
    
    printf("Playback started, waiting for completion...\n");
    while (playing > 0) {
        sleep(1);
    }

    printf("Playback finished\n");

    // Cleanup
    libvlc_media_player_stop(player);
    libvlc_media_player_release(player);
    libvlc_release(vlc);

    return 0;
}
