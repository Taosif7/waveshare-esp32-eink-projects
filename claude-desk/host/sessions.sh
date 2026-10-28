#!/usr/bin/env bash
# Watch Claude Code sessions and drive the EinkSessions panel over BLE.
set -euo pipefail

data="${HOME}/.local/share/claude-desk"
spool="${data}/spool"
progress="${data}/progress"
snooze="${data}/snooze"
sessions_dir="${HOME}/.claude/sessions"
rx_uuid="6e400002-b5a3-f393-e0a9-e50e24dcca9e"
tx_uuid="6e400003-b5a3-f393-e0a9-e50e24dcca9e"
mkdir -p "$spool" "$progress" "$snooze"

exec 9>"${data}/lock"
if ! flock -n 9; then
  echo "sessions.sh is already running" >&2
  exit 1
fi

dump_only=0
if [[ "${1:-}" == "--dump" ]]; then
  dump_only=1
fi

declare -A was_live=()
baseline=1
shown=""
gen=0
last_frame=""
last_body=""
last_beep=""
last_pick_key=""
last_pick_at=0
connected=0
rx_path=""
bt_in=""
fifo="${data}/panel.fifo"
pending_events=()

rm -f "$fifo"
mkfifo "$fifo"
exec 8<>"$fifo"

strip_ansi() {
  # A slash inside the character class would terminate sed's s/// command.
  LC_ALL=C sed -E 's/\x1b\[[0-9;?]*[@-~]//g'
}

ble_cmd() {
  printf '%s\n' "$1" >&"$bt_in"
}

enqueue() {
  pending_events+=("$1")
}

take_event() {
  if ((${#pending_events[@]} > 0)); then
    REPLY="${pending_events[0]}"
    pending_events=("${pending_events[@]:1}")
    return 0
  fi
  local timeout=${1:-0}
  # read -t 0 only polls. It returns success and leaves the byte in the fifo,
  # so a later event makes the main loop spin and never publish again.
  if [[ "$timeout" == 0 ]]; then
    read -t 0 -r <&8 || return 1
    timeout=1
  fi
  if read -t "$timeout" -r REPLY <&8 && [[ -n "$REPLY" ]]; then
    return 0
  fi
  return 1
}

start_reader() {
  local out=$1
  local inherited
  # Bash closes coproc fds in background children. A duplicate survives.
  exec {inherited}<&"$out"
  (
    hex_acc=""
    note_line() {
      local line=$1 hex=""
      [[ -n "$line" ]] || return 0
      # "Connected: yes" arrives before GATT is ready. Wait for the later success line.
      if [[ "$line" == *"Connection successful"* || "$line" == *"Already connected"* ]]; then
        printf '%s\n' UP >&8 || true
      fi
      if [[ "$line" == *"Disconnected"* || "$line" == *"Failed to connect"* ]]; then
        printf '%s\n' DOWN >&8 || true
      fi
      if [[ "$line" == *"Failed to write"* || "$line" == *"Unable to write"* ]]; then
        printf '%s\n' "$line" >&2
      fi
      # A repeat subscribe may not send another H. Notify started is enough.
      if [[ "$line" == *"Notify started"* ]]; then
        printf '%s\n' H >&8 || true
      fi
      # bluetoothctl prints "48 0a                                            H."
      if [[ "$line" =~ ^[[:space:]]*([0-9A-Fa-f]{2}([[:space:]]+[0-9A-Fa-f]{2})*)([[:space:]]{2,}.*)?$ ]]; then
        hex="${BASH_REMATCH[1]}"
      fi
      if [[ -n "$hex" ]]; then
        local chunk msg
        # $(...) strips trailing newlines, which are the line endings in the payload.
        chunk=$(printf '%s' "$hex" | xxd -r -p 2>/dev/null || true; printf X)
        chunk=${chunk%X}
        hex_acc+="$chunk"
        while [[ "$hex_acc" == *$'\n'* ]]; do
          msg=${hex_acc%%$'\n'*}
          hex_acc=${hex_acc#*$'\n'}
          if [[ -n "$msg" ]]; then
            if [[ "$msg" == P* || "$msg" == K* ]]; then
              printf 'panel %s\n' "$msg" >&2
            fi
            printf '%s\n' "$msg" >&8 || true
          fi
        done
      fi
      return 0
    }
    while IFS= read -r raw; do
      local cleaned piece
      cleaned=$(printf '%s\n' "$raw" | strip_ansi)
      # One read can hold several prompt redraws separated by carriage returns.
      while [[ "$cleaned" == *$'\r'* ]]; do
        piece=${cleaned%%$'\r'*}
        cleaned=${cleaned#*$'\r'}
        note_line "$piece"
      done
      note_line "$cleaned"
    done <&"$inherited"
  ) &
  reader_pid=$!
}

start_bt() {
  coproc BT { stdbuf -oL bluetoothctl; }
  bt_in=${BT[1]}
  start_reader "${BT[0]}"
  sleep 0.3
  ble_cmd "power on"
  ble_cmd "agent on"
  ble_cmd "default-agent"
}

find_mac() {
  local saved="" mac info
  if [[ -s "${data}/device" ]]; then
    saved=$(<"${data}/device")
    if bluetoothctl info "$saved" 2>/dev/null | grep -q 'Connected:'; then
      printf '%s\n' "$saved"
      return 0
    fi
  fi
  echo "scanning for EinkSessions" >&2
  bluetoothctl power on >/dev/null
  bluetoothctl --timeout 12 scan le >/dev/null || true
  while read -r _ mac _; do
    [[ "$mac" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]] || continue
    info=$(bluetoothctl info "$mac" 2>/dev/null || true)
    if [[ "$info" == *"EinkSessions"* || "$info" == *"6e400001-b5a3-f393-e0a9-e50e24dcca9e"* ]]; then
      printf '%s\n' "$mac" > "${data}/device"
      printf '%s\n' "$mac"
      return 0
    fi
  done < <(bluetoothctl devices)
  echo "EinkSessions not found" >&2
  return 1
}

wait_status() {
  local want=$1 timeout=$2
  local start=$SECONDS
  while ((SECONDS - start < timeout)); do
    if take_event 1; then
      if [[ "$REPLY" == "$want" ]]; then
        return 0
      fi
      enqueue "$REPLY"
    fi
  done
  return 1
}

connect_panel() {
  local mac
  if ! mac=$(find_mac); then
    connected=0
    return 1
  fi
  echo "connecting to ${mac}" >&2
  # A failed attempt leaves bluetoothctl in the gatt menu, where connect is invalid.
  ble_cmd "back"
  ble_cmd "connect ${mac}"
  if ! wait_status UP 20; then
    rm -f "${data}/device"
    connected=0
    return 1
  fi
  # UUID alone selects on the device bluetoothctl just connected.
  ble_cmd "menu gatt"
  ble_cmd "select-attribute ${tx_uuid}"
  ble_cmd "notify off"
  ble_cmd "notify on"
  if ! wait_status H 10; then
    connected=0
    return 1
  fi
  # Stay on the notify characteristic. bluetoothctl only prints values for the
  # selected one, and button presses arrive there. Writes go through busctl.
  if ! rx_path=$(find_rx_path "$mac"); then
    echo "no write characteristic" >&2
    connected=0
    return 1
  fi
  echo "linked ${mac}" >&2
  connected=1
  return 0
}

find_rx_path() {
  local mac=$1
  local dev="dev_${mac//:/_}"
  local path uuid
  while read -r path; do
    [[ "$path" == */"$dev"/*char* ]] || continue
    uuid=$(busctl get-property org.bluez "$path" org.bluez.GattCharacteristic1 UUID 2>/dev/null || true)
    if [[ "$uuid" == *"$rx_uuid"* ]]; then
      printf '%s\n' "$path"
      return 0
    fi
  done < <(busctl tree org.bluez | awk '{print $NF}')
  return 1
}

send_line() {
  local text=$1
  [[ -n "$rx_path" && -n "$text" ]] || return 0
  local -a bytes=() row=()
  local b
  while read -r -a row; do
    for b in "${row[@]}"; do
      bytes+=($((16#$b)))
    done
  done < <(printf '%s' "$text" | od -An -tx1)
  local i=0
  while ((i < ${#bytes[@]})); do
    local -a slice=("${bytes[@]:i:180}")
    if ! busctl call org.bluez "$rx_path" org.bluez.GattCharacteristic1 WriteValue aya{sv} \
      "${#slice[@]}" "${slice[@]}" 1 type s command >/dev/null; then
      echo "write failed" >&2
      return 1
    fi
    i=$((i + 180))
  done
}

one_line() {
  printf '%s' "$1" | tr '\n\r\t' '   ' | cut -c1-"$2"
}

collect_sessions() {
  local now
  now=$(date +%s)
  if [[ ! -d "$sessions_dir" ]]; then
    return
  fi
  local f base mtime age
  for f in "$sessions_dir"/*.json; do
    [[ -f "$f" ]] || continue
    base=$(basename "$f" .json)
    mtime=$(stat -c %Y "$f" 2>/dev/null || echo 0)
    age=$((now - mtime))
    jq -c --arg file "$base" --argjson age "$age" '
      def live:
        (ascii_downcase | test("archiv|expir|idle|finish")) | not;
      {
        id: (.sessionId // .session_id // .id // $file),
        title: (.aiTitle // .title // .name // .summary // .cwd // $file),
        waiting: ((.waitingFor // .waiting_for // "") | tostring),
        status: ((.status // "") | tostring),
        age: $age
      }
      | select((.status | length == 0) or (.status | live))
    ' "$f" 2>/dev/null || true
  done
}

spool_questions() {
  local f=$1
  jq -c '.questions // []' "$f" 2>/dev/null || echo '[]'
}

question_at() {
  local f=$1 idx=$2
  jq -c --argjson i "$idx" '.questions[$i] // empty' "$f"
}

snoozed() {
  local safe=$1 text=$2
  [[ -f "${snooze}/${safe}" ]] || return 1
  [[ "$(cat "${snooze}/${safe}")" == "$text" ]]
}

progress_of() {
  local safe=$1
  if [[ -f "${progress}/${safe}" ]]; then
    cat "${progress}/${safe}"
  else
    echo 0
  fi
}

mark_for() {
  local id=$1 waiting=$2 age=$3 safe=$4
  if [[ -f "${spool}/${safe}.json" ]]; then
    local idx text
    idx=$(progress_of "$safe")
    text=$(question_at "${spool}/${safe}.json" "$idx" | jq -r '.question // empty')
    if [[ -n "$text" ]] && ! snoozed "$safe" "$text"; then
      printf 'Q'
      return
    fi
  fi
  local w
  w=$(printf '%s' "$waiting" | tr '[:upper:]' '[:lower:]')
  if [[ "$w" == *error* || "$w" == *fail* ]]; then
    printf 'E'
    return
  fi
  if [[ "$w" == *input* ]]; then
    printf 'Q'
    return
  fi
  if ((age < 900)); then
    printf 'R'
    return
  fi
  printf ''
}

session_tty() {
  local sid=$1 f pid cand
  for f in "$sessions_dir"/*.json; do
    [[ -f "$f" ]] || continue
    if [[ "$(jq -r '.sessionId // .session_id // empty' "$f")" != "$sid" ]]; then
      continue
    fi
    pid=$(jq -r '.pid // empty' "$f")
    if [[ -z "$pid" || ! -e "/proc/${pid}/fd/0" ]]; then
      return 1
    fi
    cand=$(readlink -f "/proc/${pid}/fd/0" || true)
    if [[ "$cand" == /dev/pts/* || "$cand" == /dev/tty* ]]; then
      printf '%s\n' "$cand"
      return 0
    fi
    return 1
  done
  return 1
}

send_keys() {
  local tty=$1 index=$2
  if [[ "$tty" != /dev/pts/* && "$tty" != /dev/tty* ]]; then
    echo "no terminal for this session" >&2
    return 1
  fi
  if [[ ! -w "$tty" ]]; then
    echo "cannot write ${tty}" >&2
    return 1
  fi
  local i keys=""
  for ((i = 0; i < index; i++)); do
    keys+=$'\e[B'
  done
  if [[ -n "$keys" ]]; then
    printf '%s' "$keys" > "$tty"
    sleep 0.05
  fi
  # Claude's screen is raw. Enter is carriage return. A line feed scrolls it.
  printf '\r' > "$tty"
}

handle_pick() {
  local index=$1
  [[ -n "$shown" ]] || return 0
  local safe f idx q label tty nq
  safe=$(printf '%s' "$shown" | tr -c 'A-Za-z0-9._-' '_')
  f="${spool}/${safe}.json"
  [[ -f "$f" ]] || return 0
  idx=$(progress_of "$safe")
  q=$(question_at "$f" "$idx")
  [[ -n "$q" ]] || return 0
  label=$(jq -r --argjson n "$index" '.options[$n].label // empty' <<<"$q")
  tty=$(jq -r '.tty // empty' "$f")
  if [[ "$tty" != /dev/pts/* && "$tty" != /dev/tty* ]]; then
    tty=$(session_tty "$shown" || true)
  fi
  local now pick_key
  now=${EPOCHREALTIME/./}
  pick_key="${shown}:${idx}:${index}"
  if [[ "$pick_key" == "$last_pick_key" && $((now - last_pick_at)) -lt 1200000 ]]; then
    return 0
  fi
  last_pick_key=$pick_key
  last_pick_at=$now
  echo "pick ${index} ${label} -> ${tty:-none}" >&2
  if [[ -z "$label" ]] || ! send_keys "$tty" "$index"; then
    return 0
  fi
  nq=$(jq -r '.questions | length' "$f")
  if ((idx + 1 < nq)); then
    printf '%s\n' $((idx + 1)) > "${progress}/${safe}"
  fi
}

handle_dismiss() {
  [[ -n "$shown" ]] || return 0
  local safe f idx text
  safe=$(printf '%s' "$shown" | tr -c 'A-Za-z0-9._-' '_')
  f="${spool}/${safe}.json"
  [[ -f "$f" ]] || return 0
  idx=$(progress_of "$safe")
  text=$(question_at "$f" "$idx" | jq -r '.question // empty')
  if [[ -n "$text" ]]; then
    printf '%s\n' "$text" > "${snooze}/${safe}"
  fi
  shown=""
}

build_frame() {
  local -a ids=() marks=() titles=()
  local row id title waiting status age safe mark
  extra_beep=""
  while IFS= read -r row; do
    [[ -n "$row" ]] || continue
    id=$(jq -r '.id' <<<"$row")
    title=$(jq -r '.title' <<<"$row")
    waiting=$(jq -r '.waiting' <<<"$row")
    age=$(jq -r '.age' <<<"$row")
    safe=$(printf '%s' "$id" | tr -c 'A-Za-z0-9._-' '_')
    mark=$(mark_for "$id" "$waiting" "$age" "$safe")
    [[ -n "$mark" ]] || continue
    ids+=("$id")
    marks+=("$mark")
    titles+=("$(one_line "$title" 40)")
  done < <(collect_sessions)

  local active="" i
  if [[ -n "$shown" ]]; then
    for i in "${!ids[@]}"; do
      if [[ "${ids[$i]}" == "$shown" && "${marks[$i]}" == "Q" ]]; then
        safe=$(printf '%s' "$shown" | tr -c 'A-Za-z0-9._-' '_')
        if [[ -f "${spool}/${safe}.json" ]]; then
          active=$shown
        fi
      fi
    done
  fi
  if [[ -z "$active" ]]; then
    local oldest=9999999999
    for i in "${!ids[@]}"; do
      [[ "${marks[$i]}" == "Q" ]] || continue
      safe=$(printf '%s' "${ids[$i]}" | tr -c 'A-Za-z0-9._-' '_')
      [[ -f "${spool}/${safe}.json" ]] || continue
      local mt
      mt=$(stat -c %Y "${spool}/${safe}.json")
      if ((mt < oldest)); then
        oldest=$mt
        active=${ids[$i]}
      fi
    done
  fi
  shown=$active

  local frame="" body="" beep=""
  for i in "${!ids[@]}"; do
    body+="S ${marks[$i]} $(one_line "${titles[$i]}" 40)"$'\n'
  done

  if [[ -n "$active" ]]; then
    safe=$(printf '%s' "$active" | tr -c 'A-Za-z0-9._-' '_')
    local f idx q header prompt
    f="${spool}/${safe}.json"
    idx=$(progress_of "$safe")
    q=$(question_at "$f" "$idx")
    if [[ -n "$q" ]]; then
      header=$(jq -r '.header // empty' <<<"$q")
      prompt=$(jq -r '.question // empty' <<<"$q")
      if [[ -n "$header" ]]; then
        body+="T $(one_line "$header" 20)"$'\n'
      fi
      body+="Q $(one_line "$prompt" 160)"$'\n'
      local labels
      labels=$(jq -r '.options[]?.label' <<<"$q")
      while IFS= read -r label; do
        [[ -n "$label" ]] || continue
        body+="O $(one_line "$label" 36)"$'\n'
      done <<<"$labels"
      beep="Q ${safe} ${idx} ${prompt}"
    fi
  fi
  body+="!"
  if [[ "$body" != "$last_body" ]]; then
    gen=$((gen + 1))
    last_body=$body
  fi
  frame="@${gen}"$'\n'"$body"

  local idset=" "
  for i in "${!ids[@]}"; do
    idset+="${ids[$i]} "
    if [[ "$baseline" -eq 0 && -z "${was_live[${ids[$i]}]:-}" && "${marks[$i]}" == "E" ]]; then
      extra_beep="E"
    fi
  done
  if [[ "$baseline" -eq 0 ]]; then
    for old in "${!was_live[@]}"; do
      if [[ "$idset" != *" ${old} "* ]]; then
        extra_beep="D"
      fi
    done
  fi
  was_live=()
  for i in "${!ids[@]}"; do
    was_live["${ids[$i]}"]=1
  done
  baseline=0

  FRAME=$frame
  BEEP_KEY=$beep
  EXTRA_BEEP=${extra_beep:-}
}

publish() {
  build_frame
  if [[ "$dump_only" -eq 1 ]]; then
    printf '%s\n' "$FRAME"
    if [[ -n "$BEEP_KEY" ]]; then
      printf 'BEEP Q\n'
    fi
    if [[ -n "$EXTRA_BEEP" ]]; then
      printf 'BEEP %s\n' "$EXTRA_BEEP"
    fi
    return
  fi
  if [[ "$connected" -ne 1 ]]; then
    return
  fi
  if [[ "$FRAME" != "$last_frame" ]]; then
    local line
    while IFS= read -r line; do
      [[ -n "$line" ]] || continue
      send_line "${line}"$'\n'
    done <<<"$FRAME"
    last_frame=$FRAME
  fi
  if [[ -n "$BEEP_KEY" && "$BEEP_KEY" != "$last_beep" ]]; then
    send_line $'BEEP Q\n'
    last_beep=$BEEP_KEY
  fi
  if [[ -z "$BEEP_KEY" ]]; then
    last_beep=""
  fi
  if [[ -n "$EXTRA_BEEP" ]]; then
    send_line "BEEP ${EXTRA_BEEP}"$'\n'
  fi
}

main_loop() {
  start_bt
  connect_panel || true
  while true; do
    while take_event 0; do
      case "$REPLY" in
        UP) ;;
        H) connected=1 ;;
        DOWN)
          connected=0
          rx_path=""
          last_frame=""
          ;;
        K) handle_dismiss ;;
        P\ *) handle_pick "${REPLY#P }" ;;
      esac
    done
    if [[ "$connected" -ne 1 ]]; then
      connect_panel || true
    fi
    EXTRA_BEEP=""
    publish
    sleep 2
  done
}

if [[ "$dump_only" -eq 1 ]]; then
  EXTRA_BEEP=""
  publish
  exit 0
fi

trap 'kill "$reader_pid" 2>/dev/null || true' EXIT
main_loop
