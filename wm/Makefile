CC = gcc
PKGS = glib-2.0 cairo cairo-xlib
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -I../shell/include $(shell pkg-config --cflags $(PKGS))
LDLIBS = -lX11 $(shell pkg-config --libs $(PKGS))

SRC = $(wildcard src/*.c) ../shell/src/config.c ../shell/src/render.c ../shell/src/wallpaper.c
OBJ = $(SRC:.c=.o)
BIN = noco-wm

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: clean
