#!/usr/bin/env bash

transaction_begin() {
  local stamp
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  MEDIAVOL_TRANSACTION_ID="${stamp}-$$"
  MEDIAVOL_TRANSACTION_DIR="${MEDIAVOL_STATE_HOME}/transactions/${MEDIAVOL_TRANSACTION_ID}"
  run mkdir -p "$MEDIAVOL_TRANSACTION_DIR/files"
  if [[ "$MEDIAVOL_DRY_RUN" != 1 ]]; then
    printf 'created\tmode\tdestination\tbackup\n' > "$MEDIAVOL_TRANSACTION_DIR/files.tsv"
  fi
  info "Transaction: $MEDIAVOL_TRANSACTION_ID"
}

transaction_record_file() {
  local created="$1"
  local mode="$2"
  local destination="$3"
  local backup="$4"
  [[ "$MEDIAVOL_DRY_RUN" == 1 ]] && return 0
  printf '%s\t%s\t%s\t%s\n' "$created" "$mode" "$destination" "$backup" >> "$MEDIAVOL_TRANSACTION_DIR/files.tsv"
}

deploy_file() {
  local source="$1"
  local destination="$2"
  local mode="${3:-0644}"
  local current_hash desired_hash relative backup created=1

  desired_hash="$(sha256sum "$source" | cut -d' ' -f1)"
  if [[ -f "$destination" ]]; then
    current_hash="$(sha256sum "$destination" | cut -d' ' -f1)"
    [[ "$current_hash" == "$desired_hash" ]] && { info "Already current: $destination"; return 0; }

    case "$MEDIAVOL_CONFLICT" in
      preserve) warn "Preserving locally changed file: $destination"; return 0 ;;
      fail) die "File conflict: $destination" ;;
      replace) ;;
      *) die "Unknown conflict policy: $MEDIAVOL_CONFLICT" ;;
    esac

    relative="${destination#/}"
    backup="${MEDIAVOL_TRANSACTION_DIR}/files/${relative}"
    run mkdir -p "$(dirname "$backup")"
    run cp -a "$destination" "$backup"
    created=0
  else
    backup=""
  fi

  run mkdir -p "$(dirname "$destination")"
  run install -m "$mode" "$source" "$destination"
  transaction_record_file "$created" "$mode" "$destination" "$backup"
}

rollback_transaction() {
  local id="$1"
  local directory="${MEDIAVOL_STATE_HOME}/transactions/${id}"
  local created mode destination backup

  [[ -r "$directory/files.tsv" ]] || die "Unknown transaction: $id"
  mapfile -t records < <(tail -n +2 "$directory/files.tsv")
  for (( index=${#records[@]}-1; index>=0; index-- )); do
    IFS=$'\t' read -r created mode destination backup <<< "${records[$index]}"
    if [[ "$created" == 1 ]]; then
      run rm -f "$destination"
    elif [[ -n "$backup" && -e "$backup" ]]; then
      run cp -a "$backup" "$destination"
    fi
  done
}
