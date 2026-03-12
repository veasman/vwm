#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
BIN_PATH="${PREFIX}/bin/vwm"
DESKTOP_PATH="${PREFIX}/share/xsessions/vwm.desktop"

echo "==> Removing ${BIN_PATH}"
rm -f "${BIN_PATH}"

echo "==> Removing ${DESKTOP_PATH}"
rm -f "${DESKTOP_PATH}"

echo "==> Uninstall complete"
echo "Your personal config in ~/.config/vwm/ was not removed."
