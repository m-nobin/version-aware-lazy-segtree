# Pilot campaign results

Everything under this directory is the **exploratory pilot** of 21 August 2026.
It corresponds to the logical location `bench/results/pilot/` in the research
plan; the physical paths predate that layout and are preserved so the recorded
environment files, the analysis code and the report keep pointing at the data
they were generated from. Every later campaign, including confirmatory work,
must use a unique ID and live under
`bench/results/campaigns/<campaign-id>/raw/`; it must never be mixed into
these legacy pilot files.

The pilot is not confirmatory evidence. It was run on one machine, with one
primary compiler and allocator, before any analysis protocol was registered.
Its role is to find harness defects, choose estimands and precision targets,
propose hypotheses and size the confirmatory campaign.

## Provenance

| Fact | Value |
|---|---|
| Recorded | 2026-08-21 (timing campaign, then allocation campaign, same day) |
| Machine | Apple M4, 4P+6E cores, 16 GiB, macOS 26.5.2 (Darwin 25.5.0, arm64) |
| Compiler | AppleClang 21.0.0, `-O3 -DNDEBUG`, C++17, Release |
| Allocator | system allocator for timing; counting `operator new` for allocation |
| Protocol | 11 trials, 3 warm-up trials after 15 s warm-up load, seed 20260818, 4096 MiB cap, order shuffled per trial |
| Recorded Git state | base commit `1dfb0909be9a7ddd3654253d33b7fc3847eee7b9`, `git_dirty=yes` |
| Historical command | `bench/run_campaign.sh timing 11` then `bench/run_campaign.sh alloc 3` (before campaign IDs were required) |
| Raw manifest | 96 measured/provenance files; `bench/results/raw.sha256`; manifest SHA-256 `c8869bc7388dc3a8bd1d6ad95a3f792bbf9b5b4d4765f7b7b06007c43ea1bd81` |

The per-workload `environment_*` and `system_*` files under `raw/` are the
authoritative record; the table above is a summary of them. `hostname_hash`
identifies the machine without naming it.

The run was made from a dirty tree. Commit `adf65ec` subsequently captured the
pilot harness and manuscript changes, but no checksum of the uncommitted source
diff was recorded at run time, so exact equality between that commit and the
executed dirty tree cannot be independently established. This is a disclosed
pilot provenance limitation and one reason these measurements are exploratory;
confirmatory campaigns must run from a clean recorded commit.

## Layout

- `raw/`: immutable campaign output, one file family per workload W1-W12 and
  mode (timing, alloc): `runs_*.csv` (one row per recorded trial),
  `memory_*.csv` (growth samples), `environment_*.txt` (build, flags, timer
  calibration, exact command line), `system_*.txt` (machine, power, load,
  captured again before every workload). These 96 manifested files are never
  edited or regenerated.
- `figures/`, `tables/`, `summary/`: generated from `raw/` by one command:

  ```sh
  bench/verify_pilot.sh
  ```

  The command verifies all raw checksums, uses the locked analysis environment,
  regenerates tables/figures/summaries, checks the expected cell inventory and
  rebuilds `docs/benchmarking/benchmarking.pdf`. Nothing generated is edited by
  hand. Every number quoted by the report source is a LaTeX macro defined in
  `tables/facts.tex`, so each one traces to raw rows through the versioned
  `bench/analysis/report.py`.

## Rules

- The 96-entry manifest covers the historical measured and provenance
  families: `runs_*`, `memory_*`, `environment_*` and `system_*`.
  Locally present `structural_*.csv` files were derived from the operation
  streams after the campaign and are not measured pilot raw data, are not
  report inputs and are deliberately excluded from that manifest.
- Files under `raw/` are immutable history. `bench/run_campaign.sh` now requires
  a campaign ID and writes below `bench/results/campaigns/<id>/raw/`; both the
  runner and benchmark binary refuse to overwrite campaign output.
- Pilot numbers are always quoted with the label *exploratory pilot* and are
  never pooled with confirmatory measurements.
- Analysis entry points receive the intended campaign path explicitly. The
  pilot verifier is fixed to the legacy pilot paths, while confirmatory
  commands consume one uniquely named campaign directory; there is no shared
  output directory to pool accidentally.
