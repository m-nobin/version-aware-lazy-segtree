#!/usr/bin/env bash
# Regression for the non-dry-run gate in run_confirmatory.sh: a real campaign
# id may not look like a dry run or the pilot, and a real phase refuses to
# start from a dirty worktree, from a commit that does not descend from the
# registered one, or before docs/research/registration-record.md records a
# registered tag. The runner resolves everything relative to its own
# location, so this builds a throwaway repository around a copy of it and a
# stub binary; nothing here touches the real bench/results tree.
set -euo pipefail

runner="${1:?usage: $0 /path/to/run_confirmatory.sh /path/to/make_registration.sh}"
registration="${2:?usage: $0 /path/to/run_confirmatory.sh /path/to/make_registration.sh}"

scratch="$(mktemp -d -t valseg-registration-gate.XXXXXX)"
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$scratch/bench" "$scratch/docs/research" "$scratch/build/release-verify/bench"
cp "$runner" "$scratch/bench/run_confirmatory.sh"
cp "$registration" "$scratch/bench/make_registration.sh"
cp "$(dirname "$runner")/campaign_gate.sh" "$scratch/bench/campaign_gate.sh"
printf '#!/usr/bin/env bash\nexit 0\n' > "$scratch/build/release-verify/bench/valseg_bench"
chmod +x "$scratch/build/release-verify/bench/valseg_bench"
git -C "$scratch" init -q
git -C "$scratch" -c user.name=t -c user.email=t@t add -A
git -C "$scratch" -c user.name=t -c user.email=t@t commit -q -m init
commit="$(git -C "$scratch" rev-parse HEAD)"

expect_refusal() {
  local label="$1" needle="$2"; shift 2
  local output
  if output="$("$@" 2>&1)"; then
    echo "$label: expected a refusal, got success:" >&2; echo "$output" >&2; exit 1
  fi
  if [[ "$output" != *"$needle"* ]]; then
    echo "$label: expected '$needle' in:" >&2; echo "$output" >&2; exit 1
  fi
}

run() { bash "$scratch/bench/run_confirmatory.sh" "$@"; }

expect_refusal "dry-run-looking id" "must not contain" run linux-a-dryrun structural
expect_refusal "pilot-looking id" "must not contain" run pilot-rerun structural

touch "$scratch/dirty.txt"
expect_refusal "dirty worktree" "worktree is dirty" run linux-a structural
rm "$scratch/dirty.txt"

expect_refusal "no manifest" "does not descend from the registered commit" run linux-a structural

manifest="$scratch/docs/research/registration-manifest.txt"
{
  printf '# Registration manifest\n# git_commit=%s\n# git_dirty=no\n' "$commit"
  (cd "$scratch" && shasum -a 256 bench/run_confirmatory.sh)
} > "$manifest"
git -C "$scratch" -c user.name=t -c user.email=t@t add -A
git -C "$scratch" -c user.name=t -c user.email=t@t commit -q -m manifest
expect_refusal "no registration record" "does not record the registered tag" run linux-a structural

printf '| Registered tag | Pending |\n' > "$scratch/docs/research/registration-record.md"
git -C "$scratch" -c user.name=t -c user.email=t@t add -A
git -C "$scratch" -c user.name=t -c user.email=t@t commit -q -m record
expect_refusal "pending record" "does not record the registered tag" run linux-a structural

echo "registration gate refuses every unregistered start"
