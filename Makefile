# Makefile for JVC project

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = 

# Targets
all: display control

display: display.c
	$(CC) $(CFLAGS) -o display display.c -lgpiod -lspidev -lpng

control: control.c
	$(CC) $(CFLAGS) -o control control.c -lgpiod

clean:
	rm -f display control

vlc_minimal:
	$(CC) $(CFLAGS) -o vlc_minimal vlc_minimal.c -lvlc

.PHONY: all clean
