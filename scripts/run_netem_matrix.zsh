#!/usr/bin/env zsh

set -euo pipefail

SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}
SCRIPT_NAME=${0:t}
EXPERIMENT_SCRIPT="${SCRIPT_DIR}/run_netem_experiment.zsh"

DEFAULT_DURATION_SEC=600
DURATION_SEC="${RUDP_MATRIX_DURATION_SEC:-${DEFAULT_DURATION_SEC}}"
INITIAL_BUILD_POLICY="${RUDP_DOCKER_BUILD:-auto}"
CURRENT_BUILD_POLICY="${INITIAL_BUILD_POLICY}"

usage() {
  cat <<EOF
Usage:
  ${SCRIPT_NAME}
  ${SCRIPT_NAME} <duration-sec>

Environment:
  RUDP_MATRIX_DURATION_SEC   seconds to hold each experiment before teardown
                             default: ${DEFAULT_DURATION_SEC}
  RUDP_DOCKER_BUILD          auto|always|never
                             note: if set to always, only the first matrix
                             case rebuilds images; later cases reuse them

What it runs:
  1. baseline + chat
  2. baseline + events
  3. baseline + stream
  4. delay 100ms + stream
  5. loss 5% + events
  6. reorder 25% 50% + stream

Each experiment:
  * starts docker compose via run_netem_experiment.zsh
  * continuously sends traffic for the configured duration
  * tears down containers so final summaries are flushed to logs
EOF
}

require_tool() {
  local tool="$1"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "missing required tool: ${tool}" >&2
    exit 1
  fi
}

run_case() {
  local label="$1"
  local client_command="$2"
  shift 2

  echo
  echo "=== ${label}"
  echo "client_command=${client_command}"
  echo "duration_sec=${DURATION_SEC}"
  echo "docker_build_policy=${CURRENT_BUILD_POLICY}"

  export RUDP_CLIENT_COMMAND="${client_command}"
  export RUDP_DOCKER_BUILD="${CURRENT_BUILD_POLICY}"
  export RUDP_EXPERIMENT_NAME="${label}"
  "${EXPERIMENT_SCRIPT}" "$@"

  if [[ "${CURRENT_BUILD_POLICY}" == "always" ]]; then
    CURRENT_BUILD_POLICY="never"
  fi

  echo "--- waiting ${DURATION_SEC}s"
  sleep "${DURATION_SEC}"

  echo "--- tearing down ${label}"
  "${EXPERIMENT_SCRIPT}" down
  unset RUDP_CLIENT_COMMAND
  unset RUDP_EXPERIMENT_NAME
}

main() {
  require_tool docker

  if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
  fi

  if [[ $# -ge 1 ]]; then
    DURATION_SEC="$1"
  fi

  run_case "baseline-chat" "/spawn chat 4 0 5 burst" baseline
  run_case "baseline-events" "/spawn events 4 0 5 burst" baseline
  run_case "baseline-stream" "/spawn stream 4 0 5 burst" baseline
  run_case "delay-100ms-stream" "/spawn stream 4 0 5 burst" delay 100ms
  run_case "loss-5pct-events" "/spawn events 4 0 5 burst" loss 5%
  run_case "reorder-25pct-stream" "/spawn stream 4 0 5 burst" reorder 25% 50%

  echo
  echo "matrix complete"
}

main "$@"
