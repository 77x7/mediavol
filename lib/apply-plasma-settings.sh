#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

manifest="$ROOT/manifests/plasma-baseline.json"
for asset in \
  "$HOME/.local/share/plasma/plasmoids/plasmusic-toolbar/metadata.json" \
  "$HOME/.local/share/kwin/scripts/kzones/metadata.json" \
  "$HOME/.local/share/kwin/scripts/rememberwindowpositions/metadata.json"; do
  [[ -r "$asset" ]] || die "Required portable KDE asset is missing: $asset"
done

jq -e '
  (.supported_plasma | type == "string") and
  (.writes | type == "array" and length > 0 and
   all(.[]; (.file | type == "string" and length > 0 and (startswith("/") | not) and (contains("..") | not))
             and (.group | type == "string" and length > 0)
             and (.key | type == "string" and length > 0)
             and (.value | type == "string" or type == "number" or type == "boolean")
             and (.scope | type == "string" and test("^(always|asset:[A-Za-z0-9_-]+|feature:[A-Za-z0-9_-]+)$"))))
' "$manifest" >/dev/null || die "Invalid portable Plasma baseline: $manifest"

asset_available() {
  case "$1" in
    kzones) [[ -r "$HOME/.local/share/kwin/scripts/kzones/metadata.json" ]] ;;
    rememberwindowpositions) [[ -r "$HOME/.local/share/kwin/scripts/rememberwindowpositions/metadata.json" ]] ;;
    kinetic-fadingpopups) [[ -d "$HOME/.local/share/kwin/effects/kinetic-fadingpopups" ]] ;;
    *) return 1 ;;
  esac
}

scope_selected() {
  local scope="$1"
  case "$scope" in
    always) return 0 ;;
    asset:*) asset_available "${scope#asset:}" ;;
    feature:*) [[ ",${FEATURES:-}," == *,${scope#feature:},* ]] ;;
  esac
}

while IFS=$'\t' read -r file group key value scope; do
  scope_selected "$scope" || continue
  run kwriteconfig6 --file "$file" --group "$group" --key "$key" "$value"
done < <(jq -r '.writes[] | [.file, .group, .key, (.value | tostring), .scope] | @tsv' "$manifest")

if busctl --user status org.kde.KWin >/dev/null 2>&1; then
  run busctl --user call org.kde.KWin /KWin org.kde.KWin reconfigure
else
  warn "KWin is not available on the session bus; portable KDE settings will take effect after login."
fi

info "Applied the portable KDE baseline: themes, decoration, KWin effects, KZones, and bundled scripts."
