# Second machine: Linux x86-64 over SSH

The registered protocol needs one Linux x86-64 machine besides the macOS
development machine. Everything below runs from the Mac; the Linux box only
needs sshd, a login with `sudo`, and network reachability. Nothing on it is
tracked by Git, so the campaign directory is copied back with `rsync`.

## 1. Qualify the machine

Run these over `ssh` before installing anything. All three must hold, or the
box is not the registered second machine (issue #45): a virtual guest or a
machine with no cpufreq driver cannot be governor-pinned, and that
invalidates every timing row it would produce.

```sh
systemd-detect-virt            # must print "none"
ls /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor   # must exist
lscpu | grep -E 'Model name|^CPU\(s\)|Thread|L2|L3'         # x86-64, not an Apple family
```

## 2. Reach it from the Mac

```sh
ssh-keygen -t ed25519 -f ~/.ssh/valseg_linux -N ''
ssh-copy-id -i ~/.ssh/valseg_linux.pub USER@HOST
cat >> ~/.ssh/config <<'CFG'
Host bench-linux
  HostName HOST
  User USER
  IdentityFile ~/.ssh/valseg_linux
  ServerAliveInterval 30
CFG
ssh bench-linux uname -m        # x86_64
```

`bench-linux` is the only name the rest of this page uses.

## 3. Install the toolchain once

```sh
ssh bench-linux 'sudo apt-get update && sudo apt-get install --yes \
  build-essential cmake git clang python3 tmux rsync \
  linux-tools-common linux-tools-generic libmimalloc2.0 \
  && curl -LsSf https://astral.sh/uv/install.sh | sh'
```

`linux-tools-*` supplies `cpupower`; `libmimalloc2.0` is the second
allocator for `bench/run_sensitivity.sh`; `uv` runs the analysis exactly as
`bench/analysis/uv.lock` pins it.

## 4. Check out and build the exact commit

Dry runs may use any commit. A real campaign must use the commit named in
`docs/research/registration-manifest.txt`, which `bench/run_confirmatory.sh`
enforces on its own.

```sh
ssh bench-linux 'git clone https://github.com/m-nobin/version-aware-lazy-segtree.git \
  && cd version-aware-lazy-segtree && git checkout <commit> \
  && cmake --preset release-verify && cmake --build --preset release-verify --parallel \
  && ctest --preset release-verify'
```

CTest must pass completely before any phase runs. For the second-compiler
sensitivity build add `release-verify-clang` the same way.

## 5. Put the machine in the measurement state

`prepare` needs root once per boot; `record` prints what it did.

```sh
ssh -t bench-linux 'cd version-aware-lazy-segtree && sudo bench/env/pin_linux.sh prepare'
ssh bench-linux 'cd version-aware-lazy-segtree && bench/env/pin_linux.sh record'
```

Expect `governor=performance` and `intel_no_turbo=1` (or `amd_boost=0`).

## 6. Run a phase from here

Every phase is resumable, so run it inside `tmux` on the Linux side and
reattach if the SSH session drops. The dry-run form:

```sh
ssh bench-linux 'cd version-aware-lazy-segtree && tmux new -d -s valseg \
  "for phase in structural timing alloc latency trace; do \
     VALSEG_DRY_RUN=1 VALSEG_PIN=1 bash bench/run_confirmatory.sh linux-dryrun-YYYYMMDD \$phase || break; \
   done 2>&1 | tee dryrun.log"'
ssh bench-linux 'tail -3 version-aware-lazy-segtree/dryrun.log'   # progress
```

The real campaign is the same loop without `VALSEG_DRY_RUN=1` and with a
campaign id that does not contain `dryrun`; the script refuses to start it
before the protocol's registration is recorded. Sensitivity campaigns follow the
same pattern with `VALSEG_BUILD_DIR=build/release-verify-clang` and
`VALSEG_ALT_ALLOC=/usr/lib/x86_64-linux-gnu/libmimalloc.so.2`.

## 7. Verify the pin actually held

Pinning is verified from the data, not assumed: every environment file
records the affinity mask the process inherited.

```sh
ssh bench-linux 'cd version-aware-lazy-segtree && \
  grep -h core_placement bench/results/campaigns/linux-dryrun-YYYYMMDD/raw/environment_*.txt | sort | uniq -c'
```

One line, `core_placement=affinity:2`, means every process ran on the pinned
core. Any other mask means `VALSEG_PIN=1` was missing or `taskset` failed.

## 8. Copy the campaign back

```sh
rsync -a --info=progress2 \
  bench-linux:version-aware-lazy-segtree/bench/results/campaigns/linux-dryrun-YYYYMMDD/ \
  bench/results/campaigns/linux-dryrun-YYYYMMDD/
```

`bench/results/campaigns/` is ignored by Git; the copy is what the
registered analysis, the checksum stage and the paper's archive read.
