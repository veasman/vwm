# vwm

**vwm** is a lightweight tiling window manager for **X11** written in **C**.

It is built for:

- Arch Linux
- Artix Linux

## Features

- tiling layout
- monocle layout
- true fullscreen mode
- multi-monitor support
- scratchpad overlay
- lightweight status bar
- config reload via `SIGHUP`
- simple config file with includes
- workspace/class rules
- command and bind configuration

## Platform

X11 only.

## Build dependencies

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

Local install:

```bash
./install.sh
```

System install:

```bash
PREFIX=/usr/local ./install.sh
```

## Uninstall

```bash
./uninstall.sh
```

## Run

In `.xinitrc`:

```bash
exec vwm
```

## Config path

```text
~/.config/vwm/vwm.conf
```

Example config ships in:

```text
example/vwm.conf
```

## Reload

```bash
pkill -HUP -x vwm
```

## Notes

- commands are argv-based, not shell-expanded
- use absolute paths in config when you want predictable behavior
- if a command works in your shell but not through a bind, check the WM environment and prefer absolute paths
