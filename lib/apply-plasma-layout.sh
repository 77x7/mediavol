#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

script_path="$ROOT/sources/plasma-layout/adaptive-panel.js"
[[ -r "$script_path" ]] || die "Missing portable Plasma layout script: $script_path"
[[ -r "$HOME/.local/share/plasma/plasmoids/plasmusic-toolbar/metadata.json" ]] || die "Required panel widget is missing: PlasMusic Toolbar"

if [[ "${MEDIAVOL_DRY_RUN:-0}" != 1 ]] && ! busctl --user status org.kde.plasmashell >/dev/null 2>&1; then
  die "Plasma Shell is not available on the session bus; log into KDE Plasma before applying the adaptive layout."
fi

wallpaper="$(find "$HOME/.local/share/wallpapers/Layan" -type f \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' \) -print -quit 2>/dev/null || true)"
[[ -n "$wallpaper" ]] || die "Required portable wallpaper is missing from $HOME/.local/share/wallpapers/Layan"

apply_id="$(date +%s%N)"
script="$(sed \
  -e "s|__MEDIAVOL_WALLPAPER__|${wallpaper//|/\\|}|" \
  -e "s|__MEDIAVOL_APPLY_ID__|$apply_id|" \
  "$script_path")"
if [[ "${MEDIAVOL_DRY_RUN:-0}" == 1 ]]; then
  run busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s "$script"
  info "Would map one MediaVol panel to every active target display and restart Plasma Shell."
  exit 0
fi

busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s "$script" >/dev/null
mapfile -t ids < <(
  awk -v apply_id="$apply_id" '
    function emit() {
      if (containment && managed && current_apply == apply_id) print containment
    }
    /^\[/ { emit(); containment = ""; managed = 0; current_apply = "" }
    /^\[Containments\]\[[0-9]+\]\[MediaVol\]$/ {
      containment = $0
      sub(/^\[Containments\]\[/, "", containment)
      sub(/\]\[MediaVol\]$/, "", containment)
    }
    containment && $0 == "managedPanel=adaptive-bottom-panel" { managed = 1 }
    containment && /^applyId=/ { current_apply = substr($0, 9) }
    END { emit() }
  ' "$HOME/.config/plasma-org.kde.plasma.desktop-appletsrc" | sort -n
)
(( ${#ids[@]} > 0 )) || die "Plasma Shell did not create MediaVol panel containments"

if ! command -v kquitapp6 >/dev/null 2>&1; then
  die "kquitapp6 is required to persist per-display MediaVol panels"
fi
kquitapp6 plasmashell
for _ in {1..100}; do
  busctl --user status org.kde.plasmashell >/dev/null 2>&1 || break
  sleep 0.1
done
busctl --user status org.kde.plasmashell >/dev/null 2>&1 && die "Plasma Shell did not stop before per-display panel mappings were written"

for screen in "${!ids[@]}"; do
  kwriteconfig6 --file plasma-org.kde.plasma.desktop-appletsrc \
    --group Containments --group "${ids[$screen]}" --key lastScreen "$screen"
done

if command -v kstart6 >/dev/null 2>&1; then
  kstart6 plasmashell
elif command -v kstart >/dev/null 2>&1; then
  kstart plasmashell
else
  die "kstart6 or kstart is required to restart Plasma Shell after panel mapping"
fi
info "Applied one MediaVol panel per active target display. Reapplying it replaces only MediaVol-managed panels."
