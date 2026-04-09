#!/usr/bin/env zsh

set -euo pipefail

SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}
SCRIPT_NAME=${0:t}
LOG_ROOT="${REPO_ROOT}/logs"
OUTPUT_PATH="${1:-${LOG_ROOT}/experiment_summary.csv}"

usage() {
  cat <<EOF
Usage:
  ${SCRIPT_NAME}
  ${SCRIPT_NAME} <output-csv>

Exports final client/server summary lines from logs/*/* into a single CSV.
Default output:
  ${LOG_ROOT}/experiment_summary.csv
EOF
}

emit_header() {
  cat <<'EOF'
experiment,run_id,side,sent,recv,tx_bytes,rx_bytes,ctrl_tx,ctrl_rx,data_tx,data_rx,ping_sent,ping_recv,pong_sent,pong_recv,retx,rtt_ms,rtt_avg_ms,rtt_min_ms,rtt_max_ms
EOF
}

extract_value() {
  local line="$1"
  local key="$2"
  if [[ "${line}" =~ "${key}=([^ ]+)" ]]; then
    echo "${match[1]}"
  else
    echo "n/a"
  fi
}

extract_ping_half() {
  local line="$1"
  local which="$2"
  local value
  value="$(extract_value "${line}" "${which}")"
  if [[ "${value}" == */* ]]; then
    if [[ "${which}" == "ping" ]]; then
      echo "${value%%/*},${value##*/}"
    else
      echo "${value%%/*},${value##*/}"
    fi
  else
    echo "n/a,n/a"
  fi
}

summary_line() {
  local log_file="$1"
  grep ' summary ' "${log_file}" | tail -n 1
}

append_row() {
  local experiment="$1"
  local run_id="$2"
  local side="$3"
  local log_file="$4"
  local line
  line="$(summary_line "${log_file}")"
  if [[ -z "${line}" ]]; then
    return
  fi

  local sent recv tx_bytes rx_bytes ctrl_tx ctrl_rx data_tx data_rx retx
  local rtt_ms rtt_avg_ms rtt_min_ms rtt_max_ms
  local ping_values pong_values ping_sent ping_recv pong_sent pong_recv

  sent="$(extract_value "${line}" "sent")"
  recv="$(extract_value "${line}" "recv")"
  tx_bytes="$(extract_value "${line}" "tx_bytes")"
  rx_bytes="$(extract_value "${line}" "rx_bytes")"
  ctrl_tx="$(extract_value "${line}" "ctrl_tx")"
  ctrl_rx="$(extract_value "${line}" "ctrl_rx")"
  data_tx="$(extract_value "${line}" "data_tx")"
  data_rx="$(extract_value "${line}" "data_rx")"
  retx="$(extract_value "${line}" "retx")"
  rtt_ms="$(extract_value "${line}" "rtt_ms")"
  rtt_avg_ms="$(extract_value "${line}" "rtt_avg_ms")"
  rtt_min_ms="$(extract_value "${line}" "rtt_min_ms")"
  rtt_max_ms="$(extract_value "${line}" "rtt_max_ms")"

  ping_values="$(extract_ping_half "${line}" "ping")"
  pong_values="$(extract_ping_half "${line}" "pong")"
  ping_sent="${ping_values%%,*}"
  ping_recv="${ping_values##*,}"
  pong_sent="${pong_values%%,*}"
  pong_recv="${pong_values##*,}"

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "${experiment}" "${run_id}" "${side}" "${sent}" "${recv}" "${tx_bytes}" \
    "${rx_bytes}" "${ctrl_tx}" "${ctrl_rx}" "${data_tx}" "${data_rx}" \
    "${ping_sent}" "${ping_recv}" "${pong_sent}" "${pong_recv}" "${retx}" \
    "${rtt_ms}" "${rtt_avg_ms}" "${rtt_min_ms}" "${rtt_max_ms}"
}

main() {
  if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
  fi

  mkdir -p "${OUTPUT_PATH:h}"
  emit_header > "${OUTPUT_PATH}"

  local experiment_dir run_dir
  for experiment_dir in "${LOG_ROOT}"/*(/N); do
    local experiment="${experiment_dir:t}"
    for run_dir in "${experiment_dir}"/*(/N); do
      local run_id="${run_dir:t}"
      [[ -f "${run_dir}/client.log" ]] && \
        append_row "${experiment}" "${run_id}" "client" "${run_dir}/client.log" >> "${OUTPUT_PATH}"
      [[ -f "${run_dir}/server.log" ]] && \
        append_row "${experiment}" "${run_id}" "server" "${run_dir}/server.log" >> "${OUTPUT_PATH}"
    done
  done

  echo "wrote ${OUTPUT_PATH}"
}

main "$@"
