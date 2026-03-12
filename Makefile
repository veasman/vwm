PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
PKG_CONFIG ?= pkg-config

PKGS = x11 x11-xcb xcb xcb-randr xcb-icccm xcb-keysyms xft fontconfig
LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS)) -lXrender
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))

TARGET = vwm
SRC = vwm.c

.PHONY: all clean install uninstall run reload check

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

run: $(TARGET)
	./$(TARGET)

reload:
	pkill -HUP vwm

check:
	@echo "Checking build dependencies with pkg-config..."
	@$(PKG_CONFIG) --exists $(PKGS) && echo "All pkg-config deps found." || (echo "Missing dependencies." && exit 1)
