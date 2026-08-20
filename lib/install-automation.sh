#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=common.sh
source "$ROOT/lib/common.sh"
# shellcheck source=transaction.sh
source "$ROOT/lib/transaction.sh"

while IFS= read -r source; do
  relative="${source#"$ROOT/home/"}"
  destination="$HOME/$relative"
  mode=0644
  [[ "$relative" == .local/bin/* ]] && mode=0755
  if grep -q '@HOME@\|@USER@' "$source"; then
    render_file "$source" "$destination" "$mode"
  else
    deploy_file "$source" "$destination" "$mode"
  fi
done < <(find "$ROOT/home" -type f | sort)

if [[ -d "$ROOT/sources/focused-volume-app-resolver" ]]; then
  info "Building focused-volume-app-resolver"
  build_dir="${XDG_CACHE_HOME:-$HOME/.cache}/mediavol/build/focused-volume-app-resolver"
  run cmake -S "$ROOT/sources/focused-volume-app-resolver" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
  run cmake --build "$build_dir"
  run cmake --install "$build_dir"
fi

if [[ -d "$ROOT/sources/focused-volume-daemon" ]]; then
  info "Building focused-volume-daemon"
  build_dir="${XDG_CACHE_HOME:-$HOME/.cache}/mediavol/build/focused-volume-daemon"
  run cmake -S "$ROOT/sources/focused-volume-daemon" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
  run cmake --build "$build_dir"
  run cmake --install "$build_dir"
fi

run kwriteconfig6 --file kwinrc --group Plugins --key focused-volume-contextEnabled true
if command -v busctl >/dev/null 2>&1 && busctl --user status org.kde.KWin >/dev/null 2>&1; then
  run busctl --user call org.kde.KWin /KWin org.kde.KWin reconfigure
fi
run systemctl --user daemon-reload
run systemctl --user enable --now focused-volume-daemon.service
