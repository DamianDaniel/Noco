CC = gcc
PKGS = gtk+-3.0 cairo-xlib x11 glib-2.0
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -I../shell/include $(shell pkg-config --cflags $(PKGS))
LDLIBS = $(shell pkg-config --libs $(PKGS))

SRC = src/settings.c ../shell/src/wallpaper.c ../shell/src/config.c ../shell/src/render.c
OBJ = $(SRC:.c=.o)
BIN = noco-settings

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: clean
