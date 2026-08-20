// Display Image on an SPI driven sh1122 OLED display
// display connections to RPi
//   GND     ----->      GND
//   VCC     ----->      3V3
//   SCLK    ----->      SPI0 SCLK (GPIO 11)
//   SDIN    ----->      SPI0 MOSI (GPIO 10)
//   RES     ----->      GPIO 4
//   DC      ----->      GPIO 5
//   CS      ----->      SPI0 CE0 (GPIO 8)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <png.h>
#include "font4.h"

// SH1122 OLED command constants
#define SET_COL_ADR_LSB         0x00
#define SET_COL_ADR_MSB         0x10
#define SET_DISP_START_LINE     0x40
#define SET_CONTRAST            0x81
#define SET_SEG_REMAP           0xA0
#define SET_ENTIRE_ON           0xA4
#define SET_NORM_INV            0xA6
#define SET_MUX_RATIO           0xA8
#define SET_CTRL_DCDC           0xAD
#define SET_DISP                0xAE
#define SET_ROW_ADR             0xB0
#define SET_COM_OUT_DIR         0xC0
#define SET_DISP_OFFSET         0xD3
#define SET_DISP_CLK_DIV        0xD5
#define SET_PRECHARGE           0xD9
#define SET_VCOM_DESEL          0xDB
#define SET_VSEG_LEVEL          0xDC
#define SET_DISCHARGE_LEVEL     0x30

// GPIO pins
#define GPIO_RES    4
#define GPIO_DC     5

// GPIO structures for libgpiod
struct gpiod_chip *gpio_chip = NULL;
struct gpiod_line_request *gpio_request = NULL;

// GPIO control functions using libgpiod
int gpio_setup(void) {
    gpio_chip = gpiod_chip_open("/dev/gpiochip0");
    if (!gpio_chip) {
        perror("gpiod_chip_open");
        return -1;
    }

    struct gpiod_line_settings *out_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(out_settings, GPIOD_LINE_DIRECTION_OUTPUT);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    unsigned int gpio_pins[] = {GPIO_DC, GPIO_RES};
    gpiod_line_config_add_line_settings(line_cfg, gpio_pins, 2, out_settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "display");

    gpio_request = gpiod_chip_request_lines(gpio_chip, req_cfg, line_cfg);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(out_settings);

    if (!gpio_request) {
        perror("gpiod_chip_request_lines");
        gpiod_chip_close(gpio_chip);
        return -1;
    }

    return 0;
}

void gpio_cleanup(void) {
    if (gpio_request) {
        gpiod_line_request_release(gpio_request);
        gpio_request = NULL;
    }
    if (gpio_chip) {
        gpiod_chip_close(gpio_chip);
        gpio_chip = NULL;
    }
}

int gpio_write(unsigned int pin, int value) {
    if (!gpio_request) return -1;
    return gpiod_line_request_set_value(gpio_request, pin, 
                                        value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
}

static font4_t font;  // Global font instance

// FrameBuffer structure and functions
typedef struct {
    uint8_t* buffer;
    int width;
    int height;
    int buffer_size;
} FrameBuffer;

FrameBuffer* framebuffer_create(int width, int height) {
    FrameBuffer* fb = (FrameBuffer*)malloc(sizeof(FrameBuffer));
    if (!fb) return NULL;
    
    fb->width = width;
    fb->height = height;
    fb->buffer_size = (width * height) / 2;  // 4-bit per pixel
    fb->buffer = (uint8_t*)calloc(fb->buffer_size, 1);
    
    if (!fb->buffer) {
        free(fb);
        return NULL;
    }
    
    return fb;
}

void framebuffer_destroy(FrameBuffer* fb) {
    if (fb) {
        if (fb->buffer) free(fb->buffer);
        free(fb);
    }
}

void framebuffer_set_pixel(FrameBuffer* fb, int x, int y, uint8_t color) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return;
    
    int index = (y * fb->width + x) / 2;
    if (x % 2 == 0) {
        fb->buffer[index] = (fb->buffer[index] & 0x0F) | (color << 4);
    } else {
        fb->buffer[index] = (fb->buffer[index] & 0xF0) | color;
    }
}

uint8_t framebuffer_get_pixel(FrameBuffer* fb, int x, int y) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) return 0;
    
    int index = (y * fb->width + x) / 2;
    if (x % 2 == 0) {
        return fb->buffer[index] >> 4;
    } else {
        return fb->buffer[index] & 0x0F;
    }
}

void framebuffer_fill(FrameBuffer* fb, uint8_t color) {
    uint8_t color_byte = (color << 4) | color;
    memset(fb->buffer, color_byte, fb->buffer_size);
}

void framebuffer_rect(FrameBuffer* fb, int x, int y, int w, int h, uint8_t color) {
    // Draw horizontal lines
    for (int i = x; i < x + w; i++) {
        framebuffer_set_pixel(fb, i, y, color);
        framebuffer_set_pixel(fb, i, y + h - 1, color);
    }
    // Draw vertical lines
    for (int i = y; i < y + h; i++) {
        framebuffer_set_pixel(fb, x, i, color);
        framebuffer_set_pixel(fb, x + w - 1, i, color);
    }
}

void framebuffer_fill_rect(FrameBuffer* fb, int x, int y, int w, int h, uint8_t color) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < fb->width && j >= 0 && j < fb->height) {
                framebuffer_set_pixel(fb, i, j, color);
            }
        }
    }
}

void framebuffer_blit(FrameBuffer* dest, FrameBuffer* src, int dest_x, int dest_y) {
    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            int dx = dest_x + x;
            int dy = dest_y + y;
            if (dx >= 0 && dx < dest->width && dy >= 0 && dy < dest->height) {
                uint8_t pixel = framebuffer_get_pixel(src, x, y);
                framebuffer_set_pixel(dest, dx, dy, pixel);
            }
        }
    }
}

void framebuffer_draw_text(FrameBuffer* fb, font4_t* font, int x, int y, int size, const char* text) {
    render_text(font, fb->buffer, fb->width, fb->height, x, y, size, fb->width - x, (fb->width + 1) / 2, text);
}
void framebuffer_draw_text_fmt(FrameBuffer* fb, font4_t* font, int x, int y, int size, const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    framebuffer_draw_text(fb, font, x, y, size, buffer);
}

// SH1122 OLED display structure and functions
typedef struct {
    int spi_fd;
    int width;
    int height;
    FrameBuffer* fb;
    font4_t font;
} SH1122;

int sh1122_write_cmd(SH1122* oled, uint8_t cmd) {
    gpio_write(GPIO_DC, 0);  // Command mode
    
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)&cmd,
        .len = 1,
    };
    
    int ret = ioctl(oled->spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        fprintf(stderr, "Failed to send SPI command\n");
    }
    return ret;
}

int sh1122_write_data(SH1122* oled, uint8_t* data, int len) {
    gpio_write(GPIO_DC, 1);  // Data mode
    
    // Send data in chunks of 4096 bytes max
    const int chunk_size = 4096;
    int offset = 0;
    
    while (offset < len) {
        int bytes_to_send = (len - offset) > chunk_size ? chunk_size : (len - offset);
        
        struct spi_ioc_transfer tr = {
            .tx_buf = (unsigned long)(data + offset),
            .len = bytes_to_send,
        };
        
        int ret = ioctl(oled->spi_fd, SPI_IOC_MESSAGE(1), &tr);
        if (ret < 0) {
            fprintf(stderr, "Failed to send SPI data at offset %d\n", offset);
            return ret;
        }
        
        offset += bytes_to_send;
    }
    
    return offset;
}

SH1122* sh1122_create(const char* spi_device, int width, int height, int offset) {
    SH1122* oled = (SH1122*)malloc(sizeof(SH1122));
    if (!oled) return NULL;
    
    oled->width = width;
    oled->height = height;
    
    // Create framebuffer
    oled->fb = framebuffer_create(width, height);
    if (!oled->fb) {
        free(oled);
        return NULL;
    }
    
    // Open SPI device
    oled->spi_fd = open(spi_device, O_RDWR);
    if (oled->spi_fd < 0) {
        framebuffer_destroy(oled->fb);
        free(oled);
        return NULL;
    }
    
    // Configure SPI
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 4000000;  // 4 MHz
    
    ioctl(oled->spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(oled->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(oled->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    
    // Setup GPIO pins
    printf("Setting up GPIO pins...\n");
    if (gpio_setup() < 0) {
        fprintf(stderr, "Failed to setup GPIO pins\n");
        close(oled->spi_fd);
        framebuffer_destroy(oled->fb);
        free(oled);
        return NULL;
    }
    
    // Reset display
    printf("Resetting display...\n");
    gpio_write(GPIO_RES, 1);
    usleep(1000);
    gpio_write(GPIO_RES, 0);
    usleep(10000);
    gpio_write(GPIO_RES, 1);
    usleep(10000);
    
    // Initialize display
    uint8_t init_cmds[] = {
        SET_DISP | 0x00,        // Display off
        SET_COL_ADR_LSB,
        SET_COL_ADR_MSB,
        SET_DISP_START_LINE | 0x00,
        SET_SEG_REMAP,
        SET_MUX_RATIO,
        height - 1,
        SET_COM_OUT_DIR,
        SET_DISP_OFFSET,
        (0x40 - offset) & 0x3F,
        SET_CONTRAST,
        0x80,                   // Median contrast
        SET_ENTIRE_ON,
        SET_NORM_INV,
        SET_DISP | 0x01         // Display on
    };
    
    printf("Sending initialization commands...\n");
    for (int i = 0; i < sizeof(init_cmds); i++) {
        sh1122_write_cmd(oled, init_cmds[i]);
    }
    
    // Clear display
    framebuffer_fill(oled->fb, 0);
    
    printf("Display initialized successfully\n");
    return oled;
}

void sh1122_destroy(SH1122* oled) {
    if (oled) {
        if (oled->spi_fd >= 0) close(oled->spi_fd);
        framebuffer_destroy(oled->fb);
        free(oled);
    }
    gpio_cleanup();
}

void sh1122_show(SH1122* oled) {
    sh1122_write_cmd(oled, SET_COL_ADR_LSB);
    sh1122_write_cmd(oled, SET_COL_ADR_MSB);
    sh1122_write_cmd(oled, SET_ROW_ADR);
    sh1122_write_cmd(oled, 0);
    sh1122_write_data(oled, oled->fb->buffer, oled->fb->buffer_size);
}

void sh1122_poweroff(SH1122* oled) {
    sh1122_write_cmd(oled, SET_DISP | 0x00);
}

void sh1122_poweron(SH1122* oled) {
    sh1122_write_cmd(oled, SET_DISP | 0x01);
}

void sh1122_contrast(SH1122* oled, uint8_t contrast) {
    sh1122_write_cmd(oled, SET_CONTRAST);
    sh1122_write_cmd(oled, contrast);
}

void sh1122_flip(SH1122* oled) {
    sh1122_write_cmd(oled, SET_DISP_START_LINE | 0x20);
    sh1122_write_cmd(oled, 0xA1);
    sh1122_write_cmd(oled, 0xC8);
}

void sh1122_invert(SH1122* oled, int invert) {
    sh1122_write_cmd(oled, SET_NORM_INV | (invert & 1));
}

// Load PNG file and convert RGB24 to 4-bit grayscale
// Returns NULL on error, caller must free() the returned buffer
uint8_t* load_png_as_gray4(const char* filename, int* width, int* height) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }
    
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return NULL;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    
    png_init_io(png, fp);
    png_read_info(png, info);
    
    *width = png_get_image_width(png, info);
    *height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    
    // Convert to 8-bit RGB if needed
    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    
    png_read_update_info(png, info);
    
    // Allocate row pointers
    png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * (*height));
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }
    
    png_read_image(png, row_pointers);
    
    // Allocate output buffer for 4-bit grayscale
    uint8_t* gray_data = (uint8_t*)malloc((*width) * (*height));
    
    // Convert RGB24 to 4-bit grayscale using luminosity formula
    // Gray = 0.299*R + 0.587*G + 0.114*B
    for (int y = 0; y < *height; y++) {
        png_bytep row = row_pointers[y];
        for (int x = 0; x < *width; x++) {
            png_bytep px = &(row[x * 4]); // RGBA
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            
            // Convert to 8-bit grayscale
            uint8_t gray8 = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
            
            // Convert to 4-bit (0-15)
            gray_data[y * (*width) + x] = gray8 >> 4;
        }
    }
    
    // Clean up
    for (int y = 0; y < *height; y++) {
        free(row_pointers[y]);
    }
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    
    return gray_data;
}

// Main program
int main(int argc, char *argv[]) {
    printf("Initializing SH1122 OLED display...\n");
    
    SH1122* oled = sh1122_create("/dev/spidev0.0", 256, 48, 2);
    if (!oled) {
        fprintf(stderr, "Failed to initialize display\n");
        return 1;
    }
    
    printf("SH1122 OLED display driver loaded\n");
    
    // Check if a filename or tcp mode was provided
    if (argc > 1) {
        // Check for TCP mode
        if (strcmp(argv[1], "tcp") == 0) {
            // TCP server mode
            printf("Starting TCP server on port 1234...\n");
            
            int server_fd, client_fd;
            struct sockaddr_in address;
            int opt = 1;
            int addrlen = sizeof(address);
            
            // Create socket
            if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
                perror("socket failed");
                sh1122_destroy(oled);
                return 1;
            }
            
            // Set socket options
            if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
                perror("setsockopt");
                close(server_fd);
                sh1122_destroy(oled);
                return 1;
            }
            
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(1234);
            
            // Bind socket
            if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
                perror("bind failed");
                close(server_fd);
                sh1122_destroy(oled);
                return 1;
            }
            
            // Listen for connections
            if (listen(server_fd, 1) < 0) {
                perror("listen");
                close(server_fd);
                sh1122_destroy(oled);
                return 1;
            }
            
            printf("Waiting for connection on port 1234...\n");
            
            // Accept single connection
            if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                close(server_fd);
                sh1122_destroy(oled);
                return 1;
            }
            
            printf("Client connected\n");
            
            // Frame parameters
            const int img_width = 64;
            const int img_height = 48;
            const int frame_size = img_width * img_height;
            
            uint8_t* img_data = (uint8_t*)malloc(frame_size);
            if (!img_data) {
                fprintf(stderr, "Failed to allocate memory for frame\n");
                close(client_fd);
                close(server_fd);
                sh1122_destroy(oled);
                return 1;
            }
            
            // Center the image on the display (256x48)
            int offset_x = (256 - img_width) / 2;
            int offset_y = (48 - img_height) / 2;
            
            printf("Receiving frames. Press Ctrl+C to exit.\n");
            
            // Receive and display frames continuously
            while (1) {
                // Read one frame from TCP connection
                int total_read = 0;
                while (total_read < frame_size) {
                    int bytes_read = read(client_fd, img_data + total_read, frame_size - total_read);
                    if (bytes_read <= 0) {
                        printf("Connection closed or error\n");
                        free(img_data);
                        close(client_fd);
                        close(server_fd);
                        sh1122_destroy(oled);
                        return 0;
                    }
                    total_read += bytes_read;
                }
                
                // Clear display and convert 8-bit to 4-bit grayscale
                framebuffer_fill(oled->fb, 0);
                
                for (int y = 0; y < img_height; y++) {
                    for (int x = 0; x < img_width; x++) {
                        // Convert 8-bit grayscale to 4-bit (divide by 16)
                        uint8_t pixel_8bit = img_data[y * img_width + x];
                        uint8_t pixel_4bit = pixel_8bit >> 4;
                        framebuffer_set_pixel(oled->fb, offset_x + x, offset_y + y, pixel_4bit);
                    }
                }
                
                sh1122_show(oled);
            }
            
            free(img_data);
            close(client_fd);
            close(server_fd);
            
        } else {
        // Load and display PNG file (reloads continuously)
        const char* filename = argv[1];
        printf("Watching PNG file: %s (reloading at 50fps)\n", filename);
        
        printf("Press Ctrl+C to exit.\n");
        
        // Continuous reload loop at 50fps (20ms per frame)
        while (1) {
            int img_width, img_height;
            uint8_t* img_data = load_png_as_gray4(filename, &img_width, &img_height);
            
            if (img_data) {
                // Clear display
                framebuffer_fill(oled->fb, 0);
                
                // Center the image on the display (256x48)
                int offset_x = (256 - img_width) / 2;
                int offset_y = (48 - img_height) / 2;
                
                // Draw image (already in 4-bit grayscale)
                for (int y = 0; y < img_height && y < 48; y++) {
                    for (int x = 0; x < img_width && x < 256; x++) {
                        uint8_t pixel = img_data[y * img_width + x];
                        framebuffer_set_pixel(oled->fb, offset_x + x, offset_y + y, pixel);
                    }
                }
                
                sh1122_show(oled);
                free(img_data);
            } else {
                // If loading failed, just skip this frame
                // (file might be being written)
            }
            
            usleep(20000);  // 50 fps (20ms per frame)
        }
        }
        
    } else {
        // Run original animation
    font4_t font;
    font4_init(&font, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");

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
    FrameBuffer* fb = framebuffer_create(32, 40);
    memcpy(fb->buffer, image_data, sizeof(image_data));
    // render_textf(&font, fb->buffer, W, H, 16, 0, 40, W, pitch, "Value=%d", 42);
    
    // Animation loop
    for (int step = 0; step <= 128 + 16; step++) {
        framebuffer_fill(oled->fb, 0);
        framebuffer_blit(oled->fb, fb, step - 32, 0);
        
        // Draw animated rectangles at the bottom
        for (int i = 0; i < 16; i++) {
            framebuffer_fill_rect(oled->fb, i * 8, 55 - 16, 8, 8, (15 - i + step) % 16);
            framebuffer_fill_rect(oled->fb, 247 - (i * 8), 55 - 16, 8, 8, (15 - i + step) % 16);
        }
        framebuffer_rect(oled->fb, 0, 0, 256, 48, 15);
        framebuffer_draw_text_fmt(oled->fb, &font, 12, 2, 22, "Depeche Mode %02d:%02d", (step / 1) / 60, (step / 1) % 60);
        framebuffer_draw_text_fmt(oled->fb, &font, 10, 2, 32, "Enjoy the Silence");
        
        sh1122_show(oled);
        usleep(1000);  // 1ms delay
    }
    
    font4_destroy(&font);
    // Clear display at the end
    // framebuffer_fill(oled->fb, 0);
    // sh1122_show(oled);
    
    // Cleanup
    framebuffer_destroy(fb);
    }
    
    // Clean up display
    sh1122_destroy(oled);
    
    printf("Display test completed\n");
    
    return 0;
}
