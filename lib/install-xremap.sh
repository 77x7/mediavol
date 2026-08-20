#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"
source "$ROOT/lib/transaction.sh"

render_file "$ROOT/templates/focused-volume.yml.in" "$HOME/.config/xremap/focused-volume.yml" 0644
render_file "$ROOT/templates/xremap-volume-shortcuts.service.in" "$HOME/.config/systemd/user/xremap-volume-shortcuts.service" 0644

if ! id -nG | tr ' ' '\n' | grep -qx input; then
  warn "The current user is not in the input group. Run: sudo usermod -aG input $USER, then log out and back in."
fi

run systemctl --user daemon-reload
run systemctl --user enable xremap-volume-shortcuts.service
