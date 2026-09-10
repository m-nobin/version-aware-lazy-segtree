# Research documents

Supporting material for the space-time study of persistence strategies. The
correctness proof and complexity analysis are in [../proof.md](../proof.md).

| Document | Contents |
| --- | --- |
| [claim-evidence-matrix.md](claim-evidence-matrix.md) | Prior-art audit with exact primary-source locators, and the closest precedent and scoped difference for each proposed contribution |
| [capability-taxonomy.md](capability-taxonomy.md) | Semantic model for the persistence strategies: which aggregate/action policies each one admits, and the representation model the frontier results are stated in |
| [cost-model.md](cost-model.md) | Physical cost model (record, byte, allocator and RSS units) and the predictive time model with its holdout separation |
| [registered-protocol.md](registered-protocol.md) | Prospective analysis protocol: hypotheses H1 to H5, decision rules, failure policy, blinding and sensitivity paths |
| [statistical-review.md](statistical-review.md) | Independent review of the statistical machinery, recorded before registration |
| [confirmatory-campaign.md](confirmatory-campaign.md) | Runbook for executing the registered campaign |
| [registration-record.md](registration-record.md) | The commit, tag, manifest hash and timestamp the registration was fixed at |
| [registration-manifest.txt](registration-manifest.txt) | SHA-256 of every file frozen by the registration |

## Frozen files

`registration-manifest.txt` freezes the protocol, the cost model, the
statistical review and the benchmark harness by checksum. Those files are
immutable after registration: editing one invalidates the registration, so a
correction is recorded as a protocol deviation instead. They therefore keep the
drafting and review wording they carried at registration time. Verify them with:

```sh
awk 'NF && $1 !~ /^#/ {print $1"  "$2}' docs/research/registration-manifest.txt | shasum -a 256 -c -
```
