#!/usr/bin/env bash

set -o pipefail

MEDIAVOL_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MEDIAVOL_STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}/mediavol"
MEDIAVOL_DRY_RUN="${MEDIAVOL_DRY_RUN:-0}"
MEDIAVOL_CONFLICT="${MEDIAVOL_CONFLICT:-preserve}"

info() { printf '==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

run() {
  if [[ "$MEDIAVOL_DRY_RUN" == 1 ]]; then
    printf '+ '
    printf '%q ' "$@"
    printf '\n'
    return 0
  fi
  "$@"
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

render_file() {
  local source="$1"
  local destination="$2"
  local mode="${3:-0644}"
  local temporary

  temporary="$(mktemp)"
  sed \
    -e "s|@HOME@|${HOME//|/\\|}|g" \
    -e "s|@USER@|${USER//|/\\|}|g" \
    "$source" > "$temporary"
  deploy_file "$temporary" "$destination" "$mode"
  rm -f "$temporary"
}
