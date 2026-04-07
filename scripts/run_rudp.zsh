#!/usr/bin/env zsh

set -euo pipefail

SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}
SCRIPT_NAME=${0:t}
APP_BIN="${REPO_ROOT}/build/rudp_app"
DEFAULT_SERVER_CONFIG="${REPO_ROOT}/configs/server.yaml"
DEFAULT_CLIENT_CONFIG="${REPO_ROOT}/configs/client.yaml"

print_usage() {
  cat <<EOF
Usage:
  ${SCRIPT_NAME} server [server-config.yaml]
  ${SCRIPT_NAME} client [client-config.yaml]
  ${SCRIPT_NAME} both [server-config.yaml] [client-config.yaml]

Examples:
  ${SCRIPT_NAME} server
  ${SCRIPT_NAME} client
  ${SCRIPT_NAME} both
  ${SCRIPT_NAME} server configs/server.yaml
  ${SCRIPT_NAME} client configs/client.yaml
  ${SCRIPT_NAME} both configs/server.yaml configs/client.yaml
EOF
}

require_file() {
  local path="$1"
  local description="$2"
  if [[ ! -f "${path}" ]]; then
    echo "missing ${description}: ${path}" >&2
    exit 1
  fi
}

require_app() {
  if [[ ! -x "${APP_BIN}" ]]; then
    echo "missing executable: ${APP_BIN}" >&2
    echo "build it first with: cmake --preset default && cmake --build build" >&2
    exit 1
  fi
}

run_server() {
  local config_path="$1"
  require_file "${config_path}" "server config"
  require_app
  exec "${APP_BIN}" "${config_path}"
}

run_client() {
  local config_path="$1"
  require_file "${config_path}" "client config"
  require_app
  exec "${APP_BIN}" "${config_path}"
}

run_both() {
  local server_config="$1"
  local client_config="$2"

  require_file "${server_config}" "server config"
  require_file "${client_config}" "client config"
  require_app

  if ! command -v tmux >/dev/null 2>&1; then
    echo "tmux is required for 'both' mode" >&2
    exit 1
  fi

  local session_name="rudp-demo"
  if tmux has-session -t "${session_name}" 2>/dev/null; then
    session_name="rudp-demo-$(date +%s)"
  fi

  tmux new-session -d -s "${session_name}" \
    "cd '${REPO_ROOT}' && '${APP_BIN}' '${server_config}'"
  tmux split-window -h -t "${session_name}" \
    "cd '${REPO_ROOT}' && '${APP_BIN}' '${client_config}'"
  tmux select-layout -t "${session_name}" even-horizontal >/dev/null
  tmux set-option -t "${session_name}" remain-on-exit on >/dev/null
  tmux attach -t "${session_name}"
}

main() {
  local mode="${1:-}"
  case "${mode}" in
    server)
      run_server "${2:-${DEFAULT_SERVER_CONFIG}}"
      ;;
    client)
      run_client "${2:-${DEFAULT_CLIENT_CONFIG}}"
      ;;
    both)
      run_both "${2:-${DEFAULT_SERVER_CONFIG}}" "${3:-${DEFAULT_CLIENT_CONFIG}}"
      ;;
    ""|-h|--help|help)
      print_usage
      ;;
    *)
      echo "unknown mode: ${mode}" >&2
      print_usage
      exit 1
      ;;
  esac
}

main "$@"
