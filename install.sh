#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BINDIR="${BINDIR:-$PREFIX/bin}"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/vwm"
DATA_DIR="${DATA_DIR:-$PREFIX/share/vwm}"

mkdir -p "$BINDIR"
mkdir -p "$CONFIG_DIR"
mkdir -p "$DATA_DIR"

make -C "$REPO_DIR"

install -m 755 "$REPO_DIR/vwm" "$BINDIR/vwm"
install -m 644 "$REPO_DIR/example/vwm.conf" "$DATA_DIR/vwm.conf"

if [[ ! -f "$CONFIG_DIR/vwm.conf" ]]; then
	cp "$REPO_DIR/example/vwm.conf" "$CONFIG_DIR/vwm.conf"
fi

echo "installed vwm to $BINDIR/vwm"
echo "config path: $CONFIG_DIR/vwm.conf"
