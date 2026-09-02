# Confirmatory campaign runbook

The one-shot execution of the registered protocol
(`registered-protocol.md`), from registration to Gate G3. Each step names
the command and the evidence it leaves; a step with no evidence file did not
happen. Machine A is the macOS Apple Silicon development machine, machine B
the Linux x86-64 machine driven as in `bench/env/README.md`. Nothing here
runs before the registration in step 2 is recorded, and
`bench/run_confirmatory.sh` enforces that on its own. Both roles below,
analyst and custody, are performed by the authors; the custody directory is
the only thing that must stay outside every campaign tree.

## 1. Preconditions

| Requirement | Evidence |
| --- | --- |
| Both machines completed the excluded-seed dry run on the frozen harness | `bench/results/campaigns/<id>-dryrun/complete_*` on each machine |
| Statistical review approved | `statistical-review.md` |
| PR6, PR7 and PR8 merged; worktree clean | `git status --porcelain` empty |

## 2. Register

```sh
bench/make_registration.sh                       # writes registration-manifest.txt from a clean commit
bench/make_registration.sh --verify              # every frozen checksum
git add docs/research/registration-manifest.txt && git commit -m "docs(research): register the confirmatory protocol"
git tag -a registered-YYYYMMDD <commit the manifest names> -m "registered protocol"
git push origin main registered-YYYYMMDD
```

Fill every row of `registration-record.md` (commit, tag, manifest SHA-256,
the tag's push timestamp from `git log -1 --format=%cI registered-YYYYMMDD`
or the GitHub tag page, the custody directory, who verified and when) and
commit it. The record is not a frozen file, so this commit changes no
registered checksum. The registered seed is authorized from this moment and
not before. The protocol and manifest are published with the arXiv
submission of the paper.

## 3. Build and verify on each machine

```sh
git checkout registered-YYYYMMDD    # or any later commit; the runner verifies the frozen files
cmake --preset release-verify && cmake --build --preset release-verify --parallel
ctest --preset release-verify       # 100 % before any measurement
cmake --preset release-verify-clang && cmake --build --preset release-verify-clang --parallel   # Linux second compiler
cmake --preset release-verify-gcc   && cmake --build --preset release-verify-gcc   --parallel   # macOS second compiler
sudo bench/env/pin_linux.sh prepare  # Linux only, once per boot
```

Evidence: `campaign.txt` in every campaign directory records both binary
hashes, the script hashes, the registered commit and `git_dirty=no`; the
runner refuses to write it otherwise.

## 4. Seal the blinding

The custody role, from the controlled directory, before any measurement is analysed:

```sh
uv run --frozen --project bench/analysis python bench/analysis/blind.py seal   <analyst-a> --custody-dir <controlled> --study-id <study>
uv run --frozen --project bench/analysis python bench/analysis/blind.py attach <analyst-b> --custody-dir <controlled> --study-id <study>
```

Evidence: the custody directory holds the key and label map; the analyst
half never receives its path.

## 5. Measure, per machine

Campaign ids: `macos-a`, `linux-b`, then `macos-a-gcc`, `linux-b-clang`,
`macos-a-alloc`, `linux-b-alloc`. None contains `dryrun` or `pilot`.

```sh
for phase in structural timing alloc latency trace; do
  VALSEG_PIN=1 bench/run_confirmatory.sh <id> $phase
done
VALSEG_PIN=1 VALSEG_BUILD_DIR=build/release-verify-<second compiler> bench/run_confirmatory.sh <id>-<compiler> timing
VALSEG_PIN=1 VALSEG_ALT_ALLOC=<allocator library> bench/run_sensitivity.sh <id>-alloc
```

Every phase is resumable and refuses a changed binary, script, schedule or
trace on resume. Evidence per phase: `complete_<phase>` written only after
the expected-versus-present audit, `system_*.txt` before every process,
`environment_*.txt` with `core_placement` after it. Capped, incomplete and
failed trials stay in the raw CSV with their `status`; nothing is rerun.

## 6. Blind and analyse

The custody role copies each named campaign into its opaque analyst copy:

```sh
uv run --frozen --project bench/analysis python bench/analysis/blind.py blind <named-a> <analyst-a> --custody-dir <controlled> --study-id <study>
uv run --frozen --project bench/analysis python bench/analysis/blind.py blind <named-b> <analyst-b> --custody-dir <controlled> --study-id <study>
```

The analyst, who sees only the opaque copies and two lexically ordered labels:

```sh
bench/run_registered_analysis.sh analyst <analyst-a> <analyst-b> <compiler-campaign> <allocator-campaign> S0x S0y
```

The custody role, once, after the analyst half has run:

```sh
bench/run_registered_analysis.sh custodian <analyst-a> <analyst-b> <named-a> <named-b> <controlled> <study>
```

The custodian half hashes every blinded output, unblinds, verifies the named
inputs against custody manifests, runs checksums and H1, prepares, fits and
evaluates the model exactly once (training only, then one holdout read),
builds the H5 responses from machine A's trace phase, transfers, decides H5,
and re-verifies the pre-unblinding hashes. A decision that fails closed leaves
`<stage>_unavailable.json`; the half still finishes and exits non-zero.

Evidence: `analysis/` under each campaign, the study-wide pre-unblinding hash
and UTC time from `blind.py unblind`, the model artifact hash, and the
`_unavailable.json` records if any.

## 7. Archive

```sh
find bench/results/campaigns/<id>/raw -type f | sort | xargs shasum -a 256 > bench/results/campaigns/<id>/raw.sha256
```

Archive every campaign directory (raw, `campaign.txt`, schedules, traces,
`raw.sha256`, `analysis/`) as the artifact published with the paper's arXiv
submission. Pilot, dry-run and
confirmatory directories are never merged: the pilot lives under
`bench/results/raw`, dry runs carry `dryrun` in their id, and the runner
refuses that substring for a real run.

## 8. Gate G3

Classify H1 to H5 from the decision CSVs as supported, contradicted or
inconclusive; remove "predictive" from the title if H3 misses its target;
restrict hardware claims if H4 fails; foreground H5 contradictions; freeze
`claim-evidence-matrix.md` to those results. Every deviation from the
protocol is entered in its section 12 with a UTC timestamp before the
affected output is inspected. Tables and figures are generated only from the
archived campaign directories, each carrying the source rows, script, commit
and input checksums it was built from.
