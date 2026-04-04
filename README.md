# vwm

**vwm** is a lightweight tiling window manager for **X11** written in **C11**.

Built for Arch Linux and Artix Linux.

## Features

- Tiling layout with master-stack
- Monocle layout (single window focus cycling)
- True fullscreen mode
- Multi-monitor support with synced workspaces
- Scratchpad overlay with multiple autostart windows and named scratchpads
- Cairo status bar with built-in and script modules
- Workspace and float rules with monitor targeting
- Autostart with workspace/monitor placement
- Theme system with accent colors
- Config reload via `SIGHUP`
- Config file includes (max depth 16)
- Named commands and fully configurable keybinds

## Build Dependencies

Install on Arch/Artix:

```bash
doas pacman -S base-devel pkgconf \
  libx11 libxcb xcb-util-wm xcb-util-keysyms \
  libxft fontconfig cairo libxrender libxext
```

## Build

```bash
make
```

## Install

```bash
make install              # installs to /usr/local
make install PREFIX=/usr  # or system-wide
make install-config       # copy example config to ~/.config/vwm/ (won't overwrite)
```

As a pacman package:

```bash
make pkg
doas pacman -U vwm-git-*.pkg.tar.zst
```

## Uninstall

```bash
make uninstall
```

## Run

Add to `~/.xinitrc`:

```bash
exec vwm
```

## Configuration

Config path:

```
~/.config/vwm/vwm.conf
```

A fully documented example config ships in `example/vwm.conf`.

### Reload

```bash
pkill -HUP -x vwm
# or
make reload
```

### Includes

Split config across files:

```
include "~/.config/vwm/theme.conf"
include "~/.local/state/loom/generated/vwm-theme.conf"
```

### Layouts

| Layout | Description |
|--------|-------------|
| Tile | Master-stack with configurable master factor and count |
| Monocle | Single focused window, cycle with focus next/prev |
| Fullscreen | True borderless fullscreen for focused window |

New windows always take the spotlight. Moving or spawning a window onto a fullscreen workspace automatically exits fullscreen for the existing window.

### Theme

```
theme {
    bg 0x111111
    surface 0x1b1b1b
    text 0xf2f2f2
    text_muted 0x5c5c5c
    accent 0x6bacac
    accent_soft 0x458588
    border 0x353535
}
```

Theme colors derive border, bar, and workspace indicator colors at runtime.

### Scratchpad

A togglable overlay workspace. Supports multiple autostart windows and named scratchpads:

```
scratchpad {
    width_pct 94
    height_pct 94
    dim_alpha 96

    autostart "kitty sh -c 'nvim ~/TODO.md'"
    autostart "kitty -e htop"

    define "term" "kitty" class "kitty"
    define "mixer" "pavucontrol" class "pavucontrol"
}
```

Named scratchpads are toggled via keybinds. If the matching window doesn't exist, the command is spawned into the overlay:

```
binds {
    "mod+apostrophe" scratchpad          # global overlay toggle
    "mod+F1" scratchpad "term"           # toggle named scratchpad
    "mod+F2" scratchpad "mixer"
}
```

### Rules

Float and workspace rules, with optional monitor targeting:

```
rules {
    float class "pavucontrol"
    workspace 8 class "Mail"
    workspace 3 class "firefox" monitor 1
}
```

### Autostart

Run programs at startup with optional workspace/monitor placement:

```
autostart {
    run "dunst"
    run "picom --config ~/.config/picom/picom.conf"
    run "thunderbird" class "Mail" workspace 8 monitor 2
}
```

### Commands

Named commands referenced by keybinds:

```
commands {
    browser "floorp"
    launcher "rofi -show drun"
    terminal "kitty"
    vol-up "wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 5%+"
}

binds {
    "mod+w" spawn "browser"
    "mod+d" spawn "launcher"
    "mod+Return" spawn "terminal"
    "XF86AudioRaiseVolume" spawn "vol-up"
}
```

### Bar

Cairo-rendered status bar with configurable module placement, flat or pill style, and per-module state-aware coloring:

```
bar {
    enabled true
    position top
    height 24
    modules pill
    icons true
    colors true

    modules {
        left monitor
        left workspaces
        center title
        right script:cpu
        right volume
        right clock "%a %d %b %H:%M"
    }
}
```

**Built-in modules**: `monitor`, `workspaces`, `sync`, `title`, `status`, `clock`, `volume`, `network`, `battery`, `brightness`, `media`, `memory`, `weather`, `custom`.

**Script modules** with caching and configurable refresh intervals: `weather`, `printer`, `mail`, `updates`, `uptime`, `cpu`, `disk`, `swap`, `loadavg`, `kernel`, `packages`.

Script modules check for an environment variable first (e.g. `VWM_WEATHER_CMD`, `VWM_CPU_CMD`), then fall back to a built-in default command. Set the env var to override. Custom scripts auto-create when referenced: `script:my_thing` looks for `VWM_MY_THING_CMD`.

### Keybinds

All keybinds are configured in the `binds` block. There are no hardcoded defaults. See `example/vwm.conf` for a complete setup. Available actions:

| Action | Description |
|--------|-------------|
| `spawn "cmd"` | Run a named command |
| `scratchpad` | Toggle scratchpad overlay |
| `scratchpad "name"` | Toggle a named scratchpad |
| `focus_next` / `focus_prev` | Cycle focus between tiled windows |
| `focus_monitor_next` / `focus_monitor_prev` | Move focus between monitors |
| `send_monitor_next` / `send_monitor_prev` | Send window to another monitor |
| `monocle` | Toggle monocle layout |
| `fullscreen` | Toggle true fullscreen |
| `toggle_sync` | Toggle workspace sync across monitors |
| `kill_client` | Close focused window |
| `decrease_mfact` / `increase_mfact` | Resize master area |
| `zoom_master` | Swap focused window into master |
| `view_ws N` | Switch to workspace N (1-9) |
| `send_ws N` | Send window to workspace N (1-9) |
| `reload` | Reload config |
| `quit` | Exit vwm |

## Notes

- Commands are argv-based, not shell-expanded
- Use absolute paths in config for predictable behavior
- If a command works in your shell but not through a bind, check the WM environment
