PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/vwm

CC ?= cc
PKG_CONFIG ?= pkg-config

TARGET := vwm

SRC_DIR := src
INC_DIR := include
OBJ_DIR := build

SRC := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/wm.c \
	$(SRC_DIR)/x11.c \
	$(SRC_DIR)/bar.c \
	$(SRC_DIR)/layout.c \
	$(SRC_DIR)/client.c \
	$(SRC_DIR)/actions.c \
	$(SRC_DIR)/util.c

OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

WARN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic
OPT_CFLAGS ?= -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -I$(INC_DIR)
CFLAGS ?=
LDFLAGS ?=

PKGS := x11 x11-xcb xcb xcb-randr xcb-icccm xcb-keysyms xft fontconfig
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS)) -lXrender

.PHONY: all clean install uninstall run check

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(PKG_LIBS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -d "$(DESTDIR)$(DATADIR)"
	install -m 755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -m 644 example/vwm.conf "$(DESTDIR)$(DATADIR)/vwm.conf"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(DATADIR)/vwm.conf"

run: $(TARGET)
	./$(TARGET)

check:
	$(CC) $(CPPFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) $(PKG_CFLAGS) $(SRC) -fsyntax-only
