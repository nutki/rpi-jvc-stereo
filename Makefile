# Makefile for JVC project

CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags libnl-3.0 libcurl freetype2 json-c alsa libvlc)
LDLIBS = -lgpiod -lpng -lpthread -lm -lvlc $(shell pkg-config --libs libnl-3.0 libnl-genl-3.0 libcurl freetype2 json-c alsa)

# Targets
all: jvc control

jvc: jvc.o display.o font4.o wlan_check.o control.o curl.o alsavolume.o cdplayer.o

control: control.o

clean:
	rm -f jvc control

vlc_minimal: vlc_minimal.o
	$(CC) $(CFLAGS) -o $@ $^ -lvlc -lpthread

.PHONY: all clean
