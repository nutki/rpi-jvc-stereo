// Minimal libvlc video player with OSD overlay
// Compile: gcc -o vlc_minimal vlc_minimal.c -lvlc -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <vlc/vlc.h>

// OSD context structure
typedef struct {
    uint8_t *rgba_osd;      // RGBA32 OSD buffer (your existing code)
    int osd_width;
    int osd_height;
    int osd_x;              // Position to overlay
    int osd_y;
    pthread_mutex_t lock;   // Protect OSD buffer during updates
    
    // Video format info
    unsigned width;
    unsigned height;
    unsigned pitch;
} osd_context_t;

// Video callbacks
static void *lock_frame(void *opaque, void **planes) {
    osd_context_t *ctx = (osd_context_t *)opaque;
    // Allocate buffer for VLC to render into
    *planes = malloc(ctx->pitch * ctx->height);
    return *planes; // return the buffer as identifier
}

static void unlock_frame(void *opaque, void *picture, void *const *planes) {
    // Nothing needed here
}

static void display_frame(void *opaque, void *picture) {
    osd_context_t *ctx = (osd_context_t *)opaque;
    uint8_t *frame = (uint8_t *)picture;
    
    if (!frame || !ctx->rgba_osd) {
        if (frame) free(frame);
        return;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    // Composite RGBA OSD onto frame (assuming RV32/BGRA format)
    // Alpha blend: dst = src * alpha + dst * (1 - alpha)
    for (int y = 0; y < ctx->osd_height && (ctx->osd_y + y) < ctx->height; y++) {
        for (int x = 0; x < ctx->osd_width && (ctx->osd_x + x) < ctx->width; x++) {
            int osd_offset = (y * ctx->osd_width + x) * 4;
            int frame_offset = ((ctx->osd_y + y) * ctx->width + (ctx->osd_x + x)) * 4;
            
            uint8_t r = ctx->rgba_osd[osd_offset + 0];
            uint8_t g = ctx->rgba_osd[osd_offset + 1];
            uint8_t b = ctx->rgba_osd[osd_offset + 2];
            uint8_t a = ctx->rgba_osd[osd_offset + 3];
            
            if (a == 255) {
                // Fully opaque - direct copy
                frame[frame_offset + 0] = b;  // RV32 is BGRA
                frame[frame_offset + 1] = g;
                frame[frame_offset + 2] = r;
            } else if (a > 0) {
                // Alpha blend
                frame[frame_offset + 0] = (b * a + frame[frame_offset + 0] * (255 - a)) / 255;
                frame[frame_offset + 1] = (g * a + frame[frame_offset + 1] * (255 - a)) / 255;
                frame[frame_offset + 2] = (r * a + frame[frame_offset + 2] * (255 - a)) / 255;
            }
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    // Frame is automatically displayed by VLC after this callback
    
    free(frame);
}

static unsigned format_setup(void **opaque, char *chroma, unsigned *width, 
                            unsigned *height, unsigned *pitches, unsigned *lines) {
    osd_context_t *ctx = (osd_context_t *)*opaque;
    
    // Request RV32 (BGRA) format for easy compositing
    memcpy(chroma, "RV32", 4);
    
    ctx->width = *width;
    ctx->height = *height;
    ctx->pitch = *width * 4;  // 4 bytes per pixel for RGBA
    
    *pitches = ctx->pitch;
    *lines = *height;
    
    printf("Video format: %dx%d, pitch: %d\n", *width, *height, *pitches);
    return 1;
}

static void format_cleanup(void *opaque) {
    // Nothing to cleanup in format
}

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

    // Create VLC instance with scene filter parameters
    const char *vlc_args[] = {
        "--video-filter=scene",
        "--scene-height=48",
        "--scene-prefix=frame",
        "--scene-path=.",
        "--scene-replace",
        "--scene-ratio=1"
    };
    libvlc_instance_t *vlc = libvlc_new(sizeof(vlc_args)/sizeof(vlc_args[0]), vlc_args);
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

    // Initialize OSD context
    osd_context_t osd_ctx = {
        .rgba_osd = NULL,
        .osd_width = 320,    // Your OSD dimensions
        .osd_height = 240,
        .osd_x = 10,         // Position on screen
        .osd_y = 10
    };
    pthread_mutex_init(&osd_ctx.lock, NULL);
    
    // Allocate RGBA OSD buffer (your existing code would fill this)
    osd_ctx.rgba_osd = calloc(osd_ctx.osd_width * osd_ctx.osd_height, 4);
    if (!osd_ctx.rgba_osd) {
        fprintf(stderr, "Failed to allocate OSD buffer\n");
        libvlc_media_player_release(player);
        libvlc_release(vlc);
        return 1;
    }
    
    // Example: Draw red semi-transparent rectangle in OSD buffer
    for (int i = 0; i < osd_ctx.osd_width * osd_ctx.osd_height * 4; i += 4) {
        osd_ctx.rgba_osd[i + 0] = 255;  // R
        osd_ctx.rgba_osd[i + 1] = 0;    // G
        osd_ctx.rgba_osd[i + 2] = 0;    // B
        osd_ctx.rgba_osd[i + 3] = 200;  // A (semi-transparent)
    }
    
    // Set up video callbacks for OSD compositing
    // libvlc_video_set_format_callbacks(player, format_setup, format_cleanup);
    // libvlc_video_set_callbacks(player, lock_frame, unlock_frame, display_frame, &osd_ctx);

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
    
    pthread_mutex_destroy(&osd_ctx.lock);
    free(osd_ctx.rgba_osd);

    return 0;
}
