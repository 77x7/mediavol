#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"

feature_selected() { [[ ",${FEATURES:-}," == *,$1,* ]]; }

while IFS=$'\t' read -r id kind owner project feature review_required; do
  feature_selected "$feature" || continue
  case "$kind" in
    copr)
      repo_file="/etc/yum.repos.d/_copr:copr.fedorainfracloud.org:${owner}:${project}.repo"
      if [[ -e "$repo_file" ]]; then
        info "COPR already configured: ${owner}/${project}"
      else
        info "Enabling COPR: ${owner}/${project}"
        run sudo dnf copr enable -y "${owner}/${project}"
      fi
      ;;
    vendor)
      if [[ "$review_required" == true ]]; then
        warn "Vendor repository '$id' requires a reviewed bootstrap URL/GPG fingerprint and is not enabled automatically yet."
      fi
      ;;
  esac
done < <(jq -r '.required[] | [.id,.kind,(.owner // ""),(.project // ""),.feature,(.review_required // false)] | @tsv' "$ROOT/manifests/repositories.json")
