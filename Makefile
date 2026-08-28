# Makefile for JVC project

CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags freetype2) $(shell pkg-config --cflags libnl-3.0)
LDLIBS = -lgpiod -lpng -lfreetype -lpthread -lm $(shell pkg-config --libs libnl-3.0 libnl-genl-3.0)

# Targets
all: jvc control

jvc: jvc.o display.o font4.o wlan_check.o control.o

control: control.o

clean:
	rm -f jvc control

vlc_minimal: vlc_minimal.o
	$(CC) $(CFLAGS) -o $@ $^ -lvlc -lpthread

.PHONY: all clean
