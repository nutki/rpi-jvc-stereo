# Makefile for JVC project

CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags freetype2)
LDFLAGS = 

# Targets
all: display control

display: display.o font4.o
	$(CC) $(CFLAGS) -o display $^ -lgpiod -lpng -lfreetype

control: control.o
	$(CC) $(CFLAGS) -o control control.o -lgpiod

clean:
	rm -f display control

vlc_minimal: vlc_minimal.o
	$(CC) $(CFLAGS) -o vlc_minimal vlc_minimal.c -lvlc -lpthread

.PHONY: all clean
