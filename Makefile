# Makefile for JVC project

CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags freetype2)
LDLIBS = -lgpiod -lpng -lfreetype

# Targets
all: jvc control

jvc: jvc.o display.o font4.o

control: control.o

clean:
	rm -f jvc control

vlc_minimal: vlc_minimal.o
	$(CC) $(CFLAGS) -o $@ $^ -lvlc -lpthread

.PHONY: all clean
