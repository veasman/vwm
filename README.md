# vwm

**vwm** is a lightweight tiling window manager for **X11** written in **C11**.

Built for Arch Linux and Artix Linux.

## Features

- Tiling layout with master-stack
- Monocle layout (single window focus cycling)
- True fullscreen mode
- Multi-monitor support with synced workspaces
- Scratchpad overlay with multiple autostart windows and named scratchpads
- Status bar with built-in and script modules
- Workspace and float rules with monitor targeting
- Autostart with workspace/monitor placement
- Config reload via `SIGHUP`
- Config file includes (max depth 16)
- Command and keybind configuration

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

An example config ships in `example/vwm.conf`.

### Reload

```bash
pkill -HUP -x vwm
# or
make reload
```

### Layouts

| Layout | Description |
|--------|-------------|
| Tile | Master-stack with configurable master factor and count |
| Monocle | Single focused window, cycle with focus next/prev |
| Fullscreen | True borderless fullscreen for focused window |

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

### Bar Modules

Built-in modules: `monitor`, `workspaces`, `sync`, `title`, `status`, `clock`, `volume`, `network`, `battery`, `brightness`, `media`, `memory`, `weather`, `custom`.

Pre-packaged script modules with caching and configurable refresh intervals:

```
bar {
    modules {
        left monitor
        left workspaces
        center title
        right script:cpu
        right script:updates
        right script:disk
        right volume
        right clock "%a %d %b %H:%M"
    }
}
```

Available script modules: `weather`, `printer`, `mail`, `updates`, `uptime`, `cpu`, `disk`, `swap`, `loadavg`, `kernel`, `packages`.

Script modules check for an environment variable first (e.g. `VWM_WEATHER_CMD`, `VWM_CPU_CMD`), then fall back to a built-in default command. Set the env var to override.

### Default Keybinds

| Key | Action |
|-----|--------|
| `mod+Return` | Terminal |
| `mod+d` | Launcher |
| `mod+j/k` | Focus next/prev |
| `mod+h/l` | Focus monitor prev/next |
| `mod+Shift+h/l` | Send to monitor prev/next |
| `mod+f` | Toggle monocle |
| `mod+Shift+f` | Toggle fullscreen |
| `mod+q` | Kill client |
| `mod+apostrophe` | Toggle scratchpad |
| `mod+[/]` | Decrease/increase master factor |
| `mod+Shift+Return` | Zoom to master |
| `mod+s` | Toggle sync workspaces |
| `mod+1-9` | View workspace |
| `mod+Shift+1-9` | Send to workspace |
| `mod+Shift+r` | Reload config |
| `mod+Shift+q` | Quit |

## Notes

- Commands are argv-based, not shell-expanded
- Use absolute paths in config for predictable behavior
- If a command works in your shell but not through a bind, check the WM environment
