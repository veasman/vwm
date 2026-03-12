#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="${PREFIX}/bin"
XSESSIONS_DIR="${PREFIX}/share/xsessions"

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Building vwm"
make -C "${REPO_DIR}"

echo "==> Installing binary to ${BIN_DIR}"
install -d "${BIN_DIR}"
install -m 755 "${REPO_DIR}/vwm" "${BIN_DIR}/vwm"

echo "==> Installing desktop entry to ${XSESSIONS_DIR}"
install -d "${XSESSIONS_DIR}"
install -m 644 "${REPO_DIR}/vwm.desktop" "${XSESSIONS_DIR}/vwm.desktop"

echo "==> Done"
echo "Binary: ${BIN_DIR}/vwm"
echo "Desktop entry: ${XSESSIONS_DIR}/vwm.desktop"
echo
echo "Optional user config setup:"
echo "  mkdir -p ~/.config/vwm"
echo "  cp ${REPO_DIR}/examples/config.toml ~/.config/vwm/config.toml"
