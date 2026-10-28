#!/usr/bin/env bash
# Claude Code hook for AskUserQuestion.
# PreToolUse writes the question and exits without a decision, so the terminal
# dialog still appears. PostToolUse deletes that spool once either side answers.
set -euo pipefail

data="${HOME}/.local/share/claude-desk"
spool="${data}/spool"
mkdir -p "$spool" "${data}/progress" "${data}/snooze"

input=$(cat)
event=$(jq -r '.hook_event_name // empty' <<<"$input")
sid=$(jq -r '.session_id // empty' <<<"$input")
if [[ -z "$sid" ]]; then
  exit 0
fi
safe=$(printf '%s' "$sid" | tr -c 'A-Za-z0-9._-' '_')
path="${spool}/${safe}.json"

if [[ "$event" == "PostToolUse" ]]; then
  rm -f "$path" "${data}/progress/${safe}" "${data}/snooze/${safe}"
  exit 0
fi

if [[ "$event" != "PreToolUse" ]]; then
  exit 0
fi
tool=$(jq -r '.tool_name // empty' <<<"$input")
if [[ "$tool" != "AskUserQuestion" ]]; then
  exit 0
fi

tty_path=""
pid=$PPID
for _ in 1 2 3 4 5 6; do
  [[ -n "$pid" && "$pid" != 0 && "$pid" != 1 ]] || break
  if [[ -e "/proc/${pid}/fd/0" ]]; then
    cand=$(readlink -f "/proc/${pid}/fd/0" || true)
    if [[ "$cand" == /dev/pts/* || "$cand" == /dev/tty* ]]; then
      tty_path=$cand
      break
    fi
  fi
  pid=$(awk '/^PPid:/ {print $2}' "/proc/${pid}/status" 2>/dev/null || true)
done

jq -n --argjson raw "$input" --arg tty "$tty_path" --arg sid "$sid" '
  {
    session_id: $sid,
    tty: $tty,
    questions: ($raw.tool_input.questions // [])
  }
' > "${path}.tmp"
mv "${path}.tmp" "$path"
exit 0
