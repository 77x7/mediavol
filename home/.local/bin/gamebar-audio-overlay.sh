#!/usr/bin/env bash
set -euo pipefail

title="Volume Control"
pattern="pavucontrol-qt .*--qwindowtitle ${title}"

if pgrep -f "$pattern" >/dev/null; then
  pkill -f "$pattern"
  exit 0
fi

export XDG_CURRENT_DESKTOP=KDE
export KDE_SESSION_VERSION=6
export QT_QPA_PLATFORMTHEME=kde
export QT_STYLE_OVERRIDE=Breeze

pavucontrol-qt \
  --tab playback \
  --qwindowtitle "$title" \
  --qwindowgeometry 659x875+14+353 &

overlay_pid=$!
seen_focus=false

while kill -0 "$overlay_pid" 2>/dev/null; do
  active_class="$(kdotool getactivewindow getwindowclassname 2>/dev/null || true)"

  if [[ "$active_class" == "pavucontrol-qt" ]]; then
    seen_focus=true
  elif [[ "$seen_focus" == true ]]; then
    kill "$overlay_pid" 2>/dev/null || true
    exit 0
  fi

  sleep 0.2
done
