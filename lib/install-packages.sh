#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

feature_selected() {
  [[ ",${FEATURES:-}," == *,$1,* ]]
}

package_groups=(core)
feature_selected automation && package_groups+=(automation)
feature_selected plasma-style && package_groups+=(plasma-style)
feature_selected xremap && package_groups+=(xremap)
feature_selected cider && package_groups+=(cider)
feature_selected ddc && package_groups+=(ddc)
feature_selected openrgb && package_groups+=(openrgb)

mapfile -t desired < <(
  jq -r --argjson groups "$(printf '%s\n' "${package_groups[@]}" | jq -R . | jq -s .)" \
    'to_entries[] | select(.key as $key | $groups | index($key)) | .value[]' \
    "$ROOT/manifests/packages.json" | sort -u
)

missing=()
for package in "${desired[@]}"; do
  rpm -q "$package" >/dev/null 2>&1 || missing+=("$package")
done

if (( ${#missing[@]} == 0 )); then
  info "All selected RPM packages are installed."
else
  info "RPM packages to install: ${missing[*]}"
  run sudo dnf install -y "${missing[@]}"
fi
