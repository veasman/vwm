PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share

CC ?= cc
PKG_CONFIG ?= pkg-config

TARGET := vwm
BUILD_DIR := build
APPDIR := $(DATADIR)/vwm

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
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS) 2>/dev/null) -lXrender

.PHONY: all clean check deps-check install install-config uninstall pkg srcinfo reload

all: $(TARGET)

$(TARGET): check $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(PKG_LIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

check:
	@$(PKG_CONFIG) --exists $(PKGS) || { \
		echo "Missing required build dependencies."; \
		echo "On Arch/Artix install:"; \
		echo "  doas pacman -S base-devel pkgconf libx11 libxcb xcb-util-wm xcb-util-keysyms libxft fontconfig cairo libxrender libxext"; \
		exit 1; \
	}

deps-check: check
	@echo "build dependencies OK"

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -d "$(DESTDIR)$(APPDIR)"
	install -m 0644 example/vwm.conf "$(DESTDIR)$(APPDIR)/vwm.conf.example"
	@if [ -z "$(DESTDIR)" ] && [ ! -f "$(APPDIR)/vwm.conf" ]; then \
		install -m 0644 example/vwm.conf "$(APPDIR)/vwm.conf"; \
	fi

install-config:
	@mkdir -p "$(HOME)/.config/vwm"
	@if [ -f "$(HOME)/.config/vwm/vwm.conf" ]; then \
		echo "Config already exists: ~/.config/vwm/vwm.conf (not overwriting)"; \
	else \
		install -m 0644 example/vwm.conf "$(HOME)/.config/vwm/vwm.conf"; \
		echo "Installed config: ~/.config/vwm/vwm.conf"; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(APPDIR)/vwm.conf.example"
	rm -f "$(DESTDIR)$(APPDIR)/vwm.conf"

pkg:
	makepkg -fs

srcinfo:
	makepkg --printsrcinfo > .SRCINFO

reload:
	pkill -HUP -x $(TARGET) || true

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
