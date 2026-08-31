#!/usr/bin/env bash
# Put a Linux x86-64 machine into the registered measurement state, record
# what was done, and optionally run one command pinned to one core.
#
#   sudo bench/env/pin_linux.sh prepare      # set governor, disable turbo
#   bench/env/pin_linux.sh record            # print the current state
#   bench/env/pin_linux.sh run -- <command>  # taskset the command to $PIN_CORE
#
# prepare needs root and is deliberately separate from run: the campaign
# scripts call run and never escalate. PIN_CORE defaults to 2, an isolated
# physical core away from core 0's housekeeping interrupts.
set -euo pipefail

mode="${1:-record}"
core="${PIN_CORE:-2}"

record() {
  printf 'governor=%s\n' "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  printf 'intel_no_turbo=%s\n' "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo n/a)"
  printf 'amd_boost=%s\n' "$(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo n/a)"
  printf 'transparent_hugepages=%s\n' "$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
  printf 'aslr=%s\n' "$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo n/a)"
  printf 'pin_core=%s\n' "$core"
}

case "$mode" in
prepare)
  command -v cpupower >/dev/null || { echo "cpupower not installed" >&2; exit 2; }
  cpupower frequency-set --governor performance >/dev/null
  if [[ -w /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
    echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
  elif [[ -w /sys/devices/system/cpu/cpufreq/boost ]]; then
    echo 0 > /sys/devices/system/cpu/cpufreq/boost
  fi
  record
  ;;
record)
  record
  ;;
run)
  shift
  [[ "${1:-}" == "--" ]] && shift
  [[ $# -ge 1 ]] || { echo "usage: $0 run -- <command...>" >&2; exit 2; }
  exec taskset -c "$core" "$@"
  ;;
*)
  echo "usage: $0 [prepare|record|run -- <command...>]" >&2
  exit 2
  ;;
esac
