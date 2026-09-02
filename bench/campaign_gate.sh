#!/usr/bin/env bash
# The registration gate, shared by run_confirmatory.sh and run_sensitivity.sh.
# Both write evidence the registered analysis reads, so both have to refuse the
# same things: a dirty worktree, a commit that does not descend from the
# registered one, a frozen file that no longer hashes as the manifest recorded,
# and a registration record that still says the tag is pending. A dry run is
# exempt and must say so in its campaign id, so dry-run output can never be
# mistaken for confirmatory output.
#
# Sourced, not executed. Sets campaign_gate_commit, campaign_gate_dirty and
# campaign_gate_registered_commit for the caller's campaign metadata.

campaign_gate() {
  local root="$1" campaign="$2" dry="$3"
  campaign_gate_commit="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo untracked)"
  campaign_gate_dirty="$(test -n "$(git -C "$root" status --porcelain 2>/dev/null)" && echo yes || echo no)"
  campaign_gate_registered_commit=none

  if [[ -n "$dry" ]]; then
    if [[ "$campaign" != *dryrun* ]]; then
      echo "dry runs must use a campaign id containing 'dryrun'" >&2
      exit 2
    fi
    return 0
  fi

  if [[ "$campaign" == *dryrun* || "$campaign" == *pilot* ]]; then
    echo "confirmatory campaign ids must not contain 'dryrun' or 'pilot' (set VALSEG_DRY_RUN=1 for a dry run)" >&2
    exit 2
  fi
  if [[ "$campaign_gate_dirty" == "yes" ]]; then
    echo "refusing: worktree is dirty; a confirmatory campaign must start from a clean, registered commit" >&2
    exit 2
  fi
  # The manifest names the commit it was generated from, and committing the
  # manifest itself moves HEAD past that commit. What is frozen is the file
  # set: the campaign commit must descend from the registered one and every
  # frozen file must still hash exactly as the manifest recorded.
  local manifest="$root/docs/research/registration-manifest.txt"
  campaign_gate_registered_commit="$(awk -F= '/^# git_commit=/{print $2}' "$manifest" 2>/dev/null || true)"
  if [[ -z "$campaign_gate_registered_commit" ]] ||
    ! git -C "$root" merge-base --is-ancestor \
      "$campaign_gate_registered_commit" "$campaign_gate_commit" 2>/dev/null; then
    echo "refusing: commit $campaign_gate_commit does not descend from the registered commit ($manifest)" >&2
    exit 2
  fi
  "$root/bench/make_registration.sh" --verify "$manifest" >/dev/null || {
    echo "refusing: registration manifest failed checksum verification" >&2
    exit 2
  }
  local record="$root/docs/research/registration-record.md" tag_line
  tag_line="$(grep -F '| Registered tag |' "$record" 2>/dev/null || true)"
  if [[ -z "$tag_line" || "$tag_line" == *Pending* ]]; then
    echo "refusing: $record does not record the registered tag" >&2
    exit 2
  fi
}
