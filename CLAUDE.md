# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

vwm is a lightweight X11 tiling window manager written in C11. It targets Arch Linux and Artix Linux.

## Build Commands

```bash
make              # Build the vwm binary
make clean        # Remove build artifacts
make deps-check   # Verify all pkg-config dependencies are installed
make install      # Install to PREFIX (default /usr/local)
make reload       # Send SIGHUP to running vwm to reload config
make pkg          # Build pacman package via makepkg
```

There are no tests or linter commands. The CI pipeline (`ci.yml`) runs `make deps-check`, `make`, and verifies `.SRCINFO` is up to date.

## Architecture

**Global state**: A single `WM wm` global (declared in `include/vwm.h`) holds all X11 connection state, monitors, config, fonts, atoms, and the scratchpad workspace. Most modules operate on this global directly.

**Event loop** (`src/wm.c`): Poll-based loop (25ms interval) dispatching X11 events. Signal handlers set `g_should_exit` / `g_should_reload` flags checked each iteration.

**Key modules and their roles**:
- `x11.c` — X11/XCB setup: visual detection (ARGB transparency), atom interning, RandR monitor detection, root window configuration, event dispatch to handlers
- `client.c` — Window lifecycle (`manage_window`/`unmanage_client`/`scan_existing_windows`), focus management, workspace attach/detach, multi-monitor movement
- `layout.c` — Three layout modes: TILE (master-stack), MONOCLE, FLOAT. Calculates client geometry per-workspace using `mfact`, gaps, and borders
- `actions.c` — Maps keybind actions (35 `Action` enum values) to operations: spawn, focus, kill, workspace switch, layout toggle, scratchpad, etc.
- `config.c` — Parses `~/.config/vwm/vwm.conf` with block-based syntax (general, theme, bar, rules, autostart, commands, scratchpad, binds). Supports file includes (max depth 16) and hot-reload via SIGHUP
- `bar.c` / `bar_modules.c` — Cairo-based status bar with configurable modules (workspaces, title, clock, volume, network, battery, etc.) in left/center/right sections
- `system_status.c` — Reads system info (volume, network, battery, brightness, media, memory, weather) for bar modules

**Data model**: `Monitor` → has 9 `Workspace`s → each has a doubly-linked list of `Client`s. A separate `scratch_workspace` holds scratchpad clients.

**Configuration**: All config is parsed into the `Config` struct at startup and on SIGHUP reload. Commands are argv-based (no shell expansion). Keybinds map modifier+key to an `Action` enum value.

## Build Details

- Compiler flags: `-std=c11 -Wall -Wextra -Wpedantic -O2`
- Dependencies resolved via pkg-config: x11, x11-xcb, xcb, xcb-randr, xcb-icccm, xcb-keysyms, xft, fontconfig, cairo, xrender, xext
- Object files go to `build/`, final binary is `./vwm`
- Incremental builds work via Make dependency tracking
