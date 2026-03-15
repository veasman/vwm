#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
BINDIR="${BINDIR:-$PREFIX/bin}"
DATA_DIR="${DATA_DIR:-$PREFIX/share/vwm}"

rm -f "$BINDIR/vwm"
rm -f "$DATA_DIR/vwm.conf"

echo "removed vwm from $BINDIR/vwm"
echo "note: user config at ~/.config/vwm/vwm.conf was left in place"
