#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

[[ -r /etc/os-release ]] || { printf 'Cannot identify the operating system.\n' >&2; exit 1; }
# shellcheck disable=SC1091
source /etc/os-release
case "${ID:-}" in
  nobara|fedora) ;;
  *) printf 'MediaVol currently supports Nobara/Fedora KDE only (found %s).\n' "${ID:-unknown}" >&2; exit 1 ;;
esac

missing=()
for command_name in jq rsync git; do
  command -v "$command_name" >/dev/null 2>&1 || missing+=("$command_name")
done

if (( ${#missing[@]} )); then
  printf 'Missing bootstrap commands: %s\n' "${missing[*]}"
  printf 'Install them first with: sudo dnf install jq rsync git\n'
  exit 1
fi

interactive=1
for argument in "$@"; do
  case "$argument" in
    --non-interactive|--yes|--dry-run) interactive=0 ;;
  esac
done

if (( interactive )) && [[ -t 0 && -t 1 ]] && ! command -v gum >/dev/null 2>&1; then
  printf 'MediaVol uses Gum for its interactive installer. Install it now? [Y/n] '
  read -r answer
  if [[ ! "$answer" =~ ^([nN]|[nN][oO])$ ]]; then
    sudo dnf install -y gum || printf 'warning: Gum could not be installed; MediaVol will use defaults.\n' >&2
  fi
fi

exec "$ROOT/bin/mediavol" install "$@"
