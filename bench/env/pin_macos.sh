#!/usr/bin/env bash
# Put a macOS Apple Silicon machine into the registered measurement state,
# record what was checked, and optionally run one command under it.
#
#   bench/env/pin_macos.sh record            # print the current state
#   bench/env/pin_macos.sh run -- <command>  # caffeinate the command
#
# macOS offers no user-space core pinning; the benchmark binary requests the
# interactive QoS class itself, which is what keeps it on performance cores.
# This script's job is the rest: refuse to measure on battery or in low-power
# mode, and hold sleep off for the duration of a run.
set -euo pipefail

mode="${1:-record}"

power_source="$(pmset -g batt 2>/dev/null | head -1)"
low_power="$(pmset -g 2>/dev/null | awk '/lowpowermode/{print $2}')"

record() {
  printf 'power_source=%s\n' "$power_source"
  printf 'low_power_mode=%s\n' "${low_power:-n/a}"
  printf 'thermal_pressure=%s\n' "$(sysctl -n machdep.xcpm.cpu_thermal_level 2>/dev/null || echo n/a)"
  printf 'core_placement=interactive_qos_requested_by_binary\n'
}

check() {
  if [[ "$power_source" != *"AC Power"* ]]; then
    echo "refusing: on battery power; connect AC before a registered run" >&2
    exit 2
  fi
  if [[ "${low_power:-0}" == "1" ]]; then
    echo "refusing: low power mode is enabled; disable it before a registered run" >&2
    exit 2
  fi
}

case "$mode" in
record)
  record
  ;;
run)
  shift
  [[ "${1:-}" == "--" ]] && shift
  [[ $# -ge 1 ]] || { echo "usage: $0 run -- <command...>" >&2; exit 2; }
  check
  exec caffeinate -dims "$@"
  ;;
*)
  echo "usage: $0 [record|run -- <command...>]" >&2
  exit 2
  ;;
esac
