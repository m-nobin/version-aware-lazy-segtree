#!/usr/bin/env bash
# Record the machine a campaign ran on, in enough detail that someone else can
# tell whether their machine is comparable. A benchmark whose hardware is
# described as "a laptop" cannot be replicated or argued with.
#
# usage: bench/collect_environment.sh > bench/results/raw/system.txt
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"

say() { printf '%s=%s\n' "$1" "$2"; }

say collected_utc "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
say hostname_hash "$(hostname | shasum -a 256 | cut -c1-12)"
say os "$(uname -s) $(uname -r) $(uname -m)"

case "$(uname -s)" in
Darwin)
  say os_product "$(sw_vers -productName) $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
  say cpu_model "$(sysctl -n machdep.cpu.brand_string)"
  say cpu_logical "$(sysctl -n hw.ncpu)"
  say cpu_physical "$(sysctl -n hw.physicalcpu)"
  say cpu_performance_cores "$(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null || echo n/a)"
  say cpu_efficiency_cores "$(sysctl -n hw.perflevel1.logicalcpu 2>/dev/null || echo n/a)"
  say cpu_nominal_hz "$(sysctl -n hw.tbfrequency 2>/dev/null || echo n/a)"
  say cacheline_bytes "$(sysctl -n hw.cachelinesize)"
  say l1d_bytes "$(sysctl -n hw.l1dcachesize)"
  say l2_bytes "$(sysctl -n hw.l2cachesize)"
  say l3_bytes "$(sysctl -n hw.l3cachesize 2>/dev/null || echo 0)"
  say memory_bytes "$(sysctl -n hw.memsize)"
  say page_bytes "$(sysctl -n hw.pagesize)"
  say swap "$(sysctl -n vm.swapusage)"
  say power_source "$(pmset -g batt 2>/dev/null | head -1 | sed 's/.*'"'"'\(.*\)'"'"'.*/\1/')"
  say low_power_mode "$(pmset -g 2>/dev/null | awk '/lowpowermode/{print $2}')"
  say thermal_pressure "$(sysctl -n machdep.xcpm.cpu_thermal_level 2>/dev/null || echo n/a)"
  say load_average "$(sysctl -n vm.loadavg | tr -d '{}' | xargs)"
  say allocator "libmalloc (system)"
  ;;
Linux)
  say os_product "$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME")"
  say cpu_model "$(awk -F: '/model name/{print $2; exit}' /proc/cpuinfo | xargs)"
  say cpu_logical "$(nproc --all)"
  say memory_bytes "$(awk '/MemTotal/{print $2 * 1024}' /proc/meminfo)"
  say page_bytes "$(getconf PAGE_SIZE)"
  say governor "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  say turbo_disabled "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo n/a)"
  say transparent_hugepages "$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
  say aslr "$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo n/a)"
  say load_average "$(cut -d' ' -f1-3 /proc/loadavg)"
  say allocator "glibc malloc"
  ;;
esac

say compiler "$(${CXX:-c++} --version 2>/dev/null | head -1)"
say cmake "$(cmake --version 2>/dev/null | head -1)"
say git_commit "$(git -C "$root" rev-parse HEAD 2>/dev/null || echo untracked)"
say git_dirty "$(test -n "$(git -C "$root" status --porcelain 2>/dev/null)" && echo yes || echo no)"
