#!/usr/bin/env bash

set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/lib/common.sh"
source "$ROOT/lib/transaction.sh"

config="$HOME/.config/ciderctl/config"
if [[ -e "$config" ]]; then
  info "Preserving existing Cider configuration: $config"
else
  render_file "$ROOT/templates/ciderctl-config.in" "$config" 0600
  warn "Cider API credentials are intentionally not restored. Enable the local API in Cider and add its token to $config."
fi
