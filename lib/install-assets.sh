#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

if [[ "${MEDIAVOL_DRY_RUN:-0}" != 1 ]]; then
  (cd "$ROOT/assets" && sha256sum -c SHA256SUMS --status) || die "Bundled asset checksum verification failed"
fi

extract_asset() {
  local archive="$1"
  local destination="$2"
  info "Installing asset: ${archive#$ROOT/}"
  run mkdir -p "$destination"
  run tar -xzf "$archive" -C "$destination"
}

extract_asset "$ROOT/assets/themes/layan-look-and-feel.tar.gz" "$HOME/.local/share/plasma/look-and-feel"
extract_asset "$ROOT/assets/themes/layan-desktop-theme.tar.gz" "$HOME/.local/share/plasma/desktoptheme"
extract_asset "$ROOT/assets/themes/layan-dark-solid-gtk.tar.gz" "$HOME/.themes"
extract_asset "$ROOT/assets/icons/fluent-purple-dark.tar.gz" "$HOME/.local/share/icons"
extract_asset "$ROOT/assets/cursors/bibata-modern-ice.tar.gz" "$HOME/.icons"
extract_asset "$ROOT/assets/decorations/utterly-round-dark-solid.tar.gz" "$HOME/.local/share/aurorae/themes"
extract_asset "$ROOT/assets/wallpapers/layan-wallpaper.tar.gz" "$HOME/.local/share/wallpapers"
extract_asset "$ROOT/assets/kvantum/layan-solid.tar.gz" "$HOME/.config/Kvantum"
extract_asset "$ROOT/assets/plasmoids/plasmusic-toolbar.tar.gz" "$HOME/.local/share/plasma/plasmoids"
extract_asset "$ROOT/assets/kwin-scripts/kzones.tar.gz" "$HOME/.local/share/kwin/scripts"
extract_asset "$ROOT/assets/kwin-scripts/rememberwindowpositions.tar.gz" "$HOME/.local/share/kwin/scripts"
extract_asset "$ROOT/assets/kwin-effects/kinetic-fadingpopups.tar.gz" "$HOME/.local/share/kwin/effects"

if [[ ",${FEATURES:-}," == *,ddc,* ]]; then
  extract_asset "$ROOT/assets/plasmoids/ddcci-brightness.tar.gz" "$HOME/.local/share/plasma/plasmoids"
fi
