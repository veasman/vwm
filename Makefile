PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CC ?= cc
PKG_CONFIG ?= pkg-config

TARGET := vwm
SRC := vwm.c

WARN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic
OPT_CFLAGS ?= -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
CFLAGS ?=
LDFLAGS ?=

PKGS := x11 x11-xcb xcb xcb-randr xcb-icccm xcb-keysyms xft fontconfig
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS)) -lXrender

.PHONY: all clean install uninstall reload check

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS) $(CFLAGS) $(PKG_CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(PKG_LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

reload:
	pkill -x -HUP vwm

check:
	@echo "Checking build dependencies with pkg-config..."
	@$(PKG_CONFIG) --exists $(PKGS) && echo "All pkg-config deps found." || (echo "Missing dependencies." && exit 1)
