# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

vwm is a lightweight X11 tiling window manager written in C11. It targets Arch Linux and Artix Linux. The user (Charlton / veasman) is the sole developer.

## Build Commands

```bash
make              # Build the vwm binary
make clean        # Remove build artifacts
make deps-check   # Verify all pkg-config dependencies are installed
make install      # Install to PREFIX (default /usr/local)
make install-config  # Copy example config to ~/.config/vwm/ (won't overwrite)
make reload       # Send SIGHUP to running vwm to reload config
make pkg          # Build pacman package via makepkg
make srcinfo      # Regenerate .SRCINFO from PKGBUILD
```

There are no tests or linter commands. The CI pipeline (`ci.yml`) runs `make deps-check`, `make`, and verifies `.SRCINFO` is up to date. The `.SRCINFO` only reflects PKGBUILD metadata fields — changes to `build()` or `package()` functions don't affect it.

## Architecture

**Global state**: A single `WM wm` global (declared in `include/vwm.h`) holds all X11 connection state, monitors, config, fonts, atoms, and the scratchpad workspace. Most modules operate on this global directly.

**Event loop** (`src/wm.c`): Poll-based loop (25ms interval) dispatching X11 events. Signal handlers set `g_should_exit` / `g_should_reload` flags checked each iteration.

**Key modules and their roles**:
- `x11.c` — X11/XCB setup: visual detection (ARGB transparency), atom interning, RandR monitor detection, root window configuration, event dispatch to handlers
- `client.c` — Window lifecycle (`manage_window`/`unmanage_client`/`scan_existing_windows`), focus management, workspace attach/detach, multi-monitor movement. Also owns shared scratch workspace helpers: `ensure_scratch_workspace_ready()` and `scratch_visible_on_monitor()`.
- `layout.c` — Three layout modes: TILE (master-stack), MONOCLE, FLOAT. Calculates client geometry per-workspace using `mfact`, gaps, and borders. Fullscreen clients take precedence over layout modes. When a new window arrives on a fullscreen workspace, the existing fullscreen client is un-fullscreened automatically.
- `actions.c` — Maps keybind actions (35+ `Action` enum values) to operations: spawn, focus, kill, workspace switch, layout toggle, scratchpad, named scratchpad toggle, etc.
- `config.c` — Parses `~/.config/vwm/vwm.conf` with block-based syntax (general, theme, bar, rules, autostart, commands, scratchpad, binds). Supports file includes (max depth 16) and hot-reload via SIGHUP. Also contains the dynamic keybind system, prepackaged script module definitions, and named scratchpad management.
- `bar.c` / `bar_modules.c` — Cairo-based status bar with configurable modules (workspaces, title, clock, volume, network, battery, etc.) in left/center/right sections. Module style can be flat or pill.
- `system_status.c` — Reads system info (volume, network, battery, brightness, media, memory, weather) for bar modules. Each status type has independent cache timing.

**Data model**: `Monitor` -> has 9 `Workspace`s -> each has a doubly-linked list of `Client`s. A separate `scratch_workspace` holds scratchpad clients.

**Configuration**: Two config structs:
- `Config` (in `wm.config`) — runtime values: colors, geometry, font, keybinds, command argv arrays
- `DynamicConfig` (`dynconfig` global) — parsed collections: autostart entries, float/workspace rules, dynamic commands, dynamic keybinds, named scratchpads, bar module layout, script modules, theme

## Key Design Decisions

**Fullscreen vs Monocle**: Fullscreen (`is_fullscreen` on Client) hides the bar and gives a single window the full monitor. Monocle (`LAYOUT_MONOCLE` on Workspace) shows one tiled window at a time with gaps/borders, cycling with focus_next/prev. Both hide non-focused tiled windows. When any window arrives on a workspace with a fullscreen client (via spawn, cross-monitor move, or workspace send), the fullscreen client is automatically un-fullscreened so the new window takes the spotlight.

**Scratch workspace**: A single global `wm.scratch_workspace` serves as a floating overlay on whichever monitor toggles it. All scratch clients share this workspace. It supports:
- Global autostart: commands run on first toggle if scratch workspace is empty
- Named scratchpads: defined with `define "name" "cmd" class "WM_CLASS"` in scratchpad block, toggled with `scratchpad "name"` keybind, matched by WM_CLASS via xcb_icccm

**Bar script modules**: Prepackaged scripts (weather, printer, mail, updates, uptime, cpu, disk, swap, loadavg, kernel, packages) use environment variable overrides and configurable refresh intervals. Custom scripts auto-create when referenced as `script:my_thing` in bar module config.

**Commands are argv-based**: No shell expansion. `spawn()` forks, calls `setsid()`, closes the X connection fd, and `execvp()`s. Tilde expansion is handled in `expand_spawn_arg()`.

## Config Blocks

| Block | Parser function | Key items |
|-------|----------------|-----------|
| `general` | `parse_general_line` | font, font_size, border_px, gap_px, default_mfact, sync_workspaces |
| `theme` | `parse_theme_line` | bg, surface, text, text_muted, accent, accent_soft, border |
| `bar` | `parse_bar_line` | enabled, background, modules (flat/pill), icons, colors, minimal, position, height, radius, margins, gaps, padding, volume_bar_* |
| `bar > modules` | `parse_bar_modules_line` | left/center/right + module name + optional arg |
| `rules` | `parse_rules_line` | float class "X", workspace N class "X" [monitor M] |
| `autostart` | `parse_autostart_line` | run "cmd" [class "X" workspace N monitor M] |
| `commands` | `parse_commands_line` | name "command line" |
| `scratchpad` | `parse_scratchpad_overlay_line` | width_pct, height_pct, dim_alpha, command, autostart, define "name" "cmd" class "cls" |
| `binds` | `parse_binds_line` | "combo" action [arg] — spawn "cmd", scratchpad ["name"], view_ws N, send_ws N, or any builtin action |

## Dynamic Keybind System

Three kinds: `DYNKEY_BUILTIN` (Action enum), `DYNKEY_COMMAND` (named command lookup), `DYNKEY_SCRATCHPAD` (named scratchpad toggle). Parsed in `parse_binds_line`, dispatched in `execute_dynamic_keybind`.

## Build Details

- Compiler flags: `-std=c11 -Wall -Wextra -Wpedantic -O2`
- Dependencies resolved via pkg-config: x11, x11-xcb, xcb, xcb-randr, xcb-icccm, xcb-keysyms, xft, fontconfig, cairo, xrender, xext
- Object files go to `build/`, final binary is `./vwm`
- Incremental builds work via Make dependency tracking
- Install puts binary in `$(BINDIR)` and example config as `vwm.conf.example` in `$(APPDIR)` (won't overwrite existing `vwm.conf`)

## Potential Future Work

These items were discussed but not yet implemented:
- **System tray (systray)**: No XEmbed or _NET_SYSTEM_TRAY infrastructure exists yet
- **Custom bar modules**: The `BAR_MOD_CUSTOM` type exists (runs a shell command every draw cycle, no caching) but could be improved. Script modules (`BAR_MOD_SCRIPT`) are the preferred approach with caching.
- **Named scratchpad autostart**: Named scratchpads currently only spawn on first toggle of that specific keybind. They could optionally pre-spawn at WM startup.

## Code Conventions

- Static functions for file-local helpers, non-static for cross-module APIs declared in corresponding header
- Shared scratch workspace helpers live in `client.c` (not duplicated across files)
- Error messages go to stderr with `vwm:` prefix
- `CLAMP`, `MAX`, `MIN` macros defined in `vwm.h`
- Config parser uses `split_line_tokens` for tokenization, `config_unquote_inplace` for quoted strings
