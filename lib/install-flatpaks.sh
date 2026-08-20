#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

install_scope() {
  local scope="$1"
  local flag="--${scope}"
  local application origin branch local_bundle

  while IFS=$'\t' read -r application origin branch local_bundle; do
    if flatpak info "$flag" "$application" >/dev/null 2>&1; then
      info "Flatpak already installed (${scope}): $application"
      continue
    fi
    if [[ "$local_bundle" == true ]]; then
      warn "Skipping $application: a reviewed local Flatpak bundle is required."
      continue
    fi
    info "Installing ${scope} Flatpak: $application ($origin/$branch)"
    run flatpak install "$flag" -y "$origin" "${application}//${branch}"
  done < <(jq -r --arg scope "$scope" '.[$scope][] | [.application,.origin,.branch,(.local_bundle_required // false)] | @tsv' "$ROOT/manifests/flatpaks.json")
}

if [[ ",${FEATURES:-}," == *,flatpaks-user,* ]]; then
  install_scope user
fi
if [[ ",${FEATURES:-}," == *,flatpaks-system,* ]]; then
  install_scope system
fi
exit 0
