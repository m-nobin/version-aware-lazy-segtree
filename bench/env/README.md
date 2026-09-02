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

## 2. Reach it from the Mac without a password

Once, at the Linux machine's own keyboard, make sure it accepts SSH and find
its address:

```sh
sudo apt-get install --yes openssh-server
sudo systemctl enable --now ssh
hostname -I | awk '{print $1}'      # the HOST used below
whoami                              # the USER used below
```

Everything after this runs on the Mac. `ssh-copy-id` asks for the Linux
password once and never again; it installs the public key so later logins are
key-only.

```sh
ssh-keygen -t ed25519 -f ~/.ssh/valseg_linux -N ''
ssh-copy-id -i ~/.ssh/valseg_linux.pub USER@HOST
cat >> ~/.ssh/config <<'CFG'
Host bench-linux
  HostName HOST
  User USER
  IdentityFile ~/.ssh/valseg_linux
  IdentitiesOnly yes
  ServerAliveInterval 30
CFG
chmod 600 ~/.ssh/config
```

Verify the login is passwordless and lands on the right architecture. The
first command must print `x86_64` without prompting:

```sh
ssh bench-linux uname -m
ssh -o BatchMode=yes bench-linux true && echo "key auth ok"
```

`BatchMode=yes` disables every interactive prompt, so it fails loudly if the
key was not installed rather than silently falling back to a password.

If the machine sleeps or drops off the network mid-campaign, the phase stops
and resumes cleanly on the next run; nothing is lost. To prevent the pause:

```sh
ssh -t bench-linux 'sudo systemctl mask sleep.target suspend.target hibernate.target'
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
before the protocol's registration is recorded.

Both sensitivity arms are separate campaigns run through
`bench/run_sensitivity.sh`, which measures the sixteen registered cells at
twenty trials each:

```sh
bash bench/run_sensitivity.sh linux-b-clang build/release-verify-clang
VALSEG_ALT_ALLOC=/usr/lib/x86_64-linux-gnu/libmimalloc.so.2 \
  bash bench/run_sensitivity.sh linux-b-alloc
```

`libmimalloc.so.2` comes from the `libmimalloc2.0` package; install it before
the campaign, because the allocator stage refuses a campaign whose recorded
`malloc_provider` matches the primary one's.

## 6a. Surviving a power cut

A campaign runs for about a day, and mains power is not guaranteed for a day.
Nothing about that needs new machinery in the harness: every phase is
resumable, a cell counts as done only when its runs file holds data, and the
resume guard refuses a changed binary, script, schedule, seed or commit. What
is needed is something to start the resume.

Drive the campaign from a supervisor script rather than a bare loop, and give
it a `@reboot` crontab entry:

```sh
@reboot /bin/bash $HOME/valseg-supervisor.sh >> $HOME/campaign-supervisor.log 2>&1
```

The supervisor loops the five phases and the sensitivity arms, skipping what is
already complete, and holds two guards against a second measurement process: an
atomic lock directory reclaimed if the pid in it is gone, and a refusal when any
`run_confirmatory.sh`, `run_sensitivity.sh` or `valseg_bench` process is alive.
Two measured processes on one pinned core contaminate whatever the first one is
timing, and a power cut leaves exactly the kind of stale lock that would
otherwise block every later start.

Set the machine's firmware to power on when mains returns (`Restore on AC Power
Loss` on most boards). Without it the box stays off and nothing resumes.

On macOS the supervisor waits for AC rather than measuring on battery, because
the harness refuses on battery by design: DVFS and thermal behaviour differ
there, and a campaign measured half on each is not one campaign. It also holds
`caffeinate -dims` across the whole run, since `VALSEG_PIN` caffeinates only
each measured process and leaves the gaps between them uncovered.

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
