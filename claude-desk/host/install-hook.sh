#!/usr/bin/env bash
# Merge the AskUserQuestion hook into ~/.claude/settings.json.
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
cmd="${root}/ask-hook.sh"
settings="${HOME}/.claude/settings.json"
mkdir -p "${HOME}/.claude"
if [[ ! -f "$settings" ]]; then
  printf '%s\n' '{}' > "$settings"
fi
if ! jq -e . "$settings" >/dev/null; then
  echo "install-hook: ${settings} is not JSON" >&2
  exit 1
fi

tmp=$(mktemp)
jq --arg cmd "$cmd" '
  def drop:
    map(select(any(.hooks[]?; .command == $cmd) | not));
  .hooks.PreToolUse = ((.hooks.PreToolUse // []) | drop)
    + [{"matcher": "AskUserQuestion", "hooks": [{"type": "command", "command": $cmd}]}]
  | .hooks.PostToolUse = ((.hooks.PostToolUse // []) | drop)
    + [{"matcher": "AskUserQuestion", "hooks": [{"type": "command", "command": $cmd}]}]
' "$settings" > "$tmp"
mv "$tmp" "$settings"
echo "installed ${cmd}"
echo "into ${settings}"
