PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CC ?= cc
PKG_CONFIG ?= pkg-config

TARGET := vwm
BUILD_DIR := build

SRC := \
	src/main.c \
	src/config.c \
	src/wm.c \
	src/x11.c \
	src/bar.c \
	src/layout.c \
	src/client.c \
	src/actions.c \
	src/util.c \
	src/system_status.c \
	src/bar_modules.c

OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

WARN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic
OPT_CFLAGS ?= -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?=
LDFLAGS ?=

PKGS := x11 x11-xcb xcb xcb-randr xcb-icccm xcb-keysyms xft fontconfig cairo xrender xext
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS)) -lXrender

.PHONY: all clean install uninstall run reload check

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(PKG_LIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	./install.sh

uninstall:
	./uninstall.sh

run: $(TARGET)
	./$(TARGET)

reload:
	pkill -HUP -x $(TARGET) || true

check: $(TARGET)
	@echo "build ok"
