# Semantic model and strategy capability taxonomy

This document states the algebraic policy model, audits which implemented
persistence strategy supports which action class, and fixes the
representation model used by the planned lower-bound attempt. Every claim
here is either checked by `tests/policy_test.cpp`, cited to source, or
marked as a proof obligation for a later theory PR. Nothing in this document
is a theorem until that PR's independent review passes.

## 1. Operation model

One partial-persistence interface, shared by every strategy in the
comparison:

- `initialize(values)` publishes version 0;
- a range action on `[l, r]` applies to the latest version only and
  publishes exactly one new version;
- a range aggregate query reads any published version and allocates nothing
  (strategy-specific exceptions are recorded in the matrix below);
- histories are in-memory and append-only; there is no branching from
  historical versions, no deletion and no concurrency.

## 2. Aggregate and action policy

A policy provides, for aggregate states `S`, actions `A` and nonnegative
segment lengths:

```text
combine(x, y)                 -- ordered aggregate composition
aggregateIdentity()
compose(newer, older)         -- apply older first, then newer
actionIdentity()
apply(action, aggregate, len)
```

with the laws (quantified over valid aggregates and lengths; the only valid
aggregate at length zero is the identity):

```text
combine associative, identity aggregateIdentity()
compose associative, identity actionIdentity()
apply(actionIdentity(), x, len) = x
apply(compose(g, f), x, len) = apply(g, apply(f, x, len), len)
apply(f, combine(x, y), lenX + lenY) = combine(apply(f, x, lenX), apply(f, y, lenY))
apply(f, aggregateIdentity(), 0) = aggregateIdentity()
```

`combine` need not commute; children are combined in index order.
Constant-time operations, fixed-size states, recognizable identities and
defined arithmetic are implementation assumptions, not algebraic laws.

The composition convention is fixed as `compose(newer, older)`: the older
action acts first. Every diagram, proof and implementation uses this
direction; `include/valseg/policy.hpp` documents it at the definition site.

### 2.1 Instantiations

| Policy | Definition | Arithmetic | Role |
| --- | --- | --- | --- |
| `SumAddPolicy` | ordered sum; additive actions; `apply(d, s, len) = s + d*len` | two's-complement wraparound, laws exact for all inputs | the full performance instantiation; what every existing structure hard-codes |
| `MinAddPolicy` | ordered minimum; additive actions; `apply(d, m, len) = m + d` for `len > 0` | wraparound-defined; laws hold on the no-wrap domain | structural and implementation-generality check |
| `AffineSumModPolicy<p>` | `x -> a*x + b` mod `p`; `apply((a,b), s, len) = a*s + b*len` | modular, laws exact for all inputs | minimal noncommutative witness; positive control for order-preserving strategies |

`tests/policy_test.cpp` checks every law exhaustively over small domains
(and exhaustively over all actions and residues for `AffineSumModPolicy<13>`),
verifies that the induced transformations of SumAdd and MinAdd commute on
those domains, and pins the minimal AffineSum counterexample: with older
action `x -> x + 1` and newer `x -> 2x`, the two application orders give 2
and 1. Tests validate these implementations, not generic theorems.

### 2.2 Observational action image

For length `len`, an action `f` induces the transformation
`rho_len(f)(x) = apply(f, x, len)` on valid aggregates. Two actions are
observationally equivalent when they induce the same transformation at every
valid length. Correctness of the tag-retaining subject depends on the image
`rho(A)`, not on the action representation: the subject accumulates retained
ancestor tags outermost-first during descent, which reverses chronological
order, so it computes `rho(f1) ... rho(fk)` where the semantics require
`rho(fk) ... rho(f1)`. The two agree exactly when the induced
transformations commute pairwise on reachable aggregates.

Proof obligations (theory PR, Gate G2): sufficiency of induced
commutativity; necessity under explicit reachability and order-separation
assumptions; the faithful-action corollary. Until that review passes, this
project claims only what the tests witness: commutation on the tested
domains and the concrete counterexample.

## 3. Capability facts in code

`include/valseg/policy.hpp` gives each policy two `constexpr bool` facts:

- `kInducedActionsCommute`: whether the induced transformations commute;
  the property the tag-retaining subject needs;
- `kCheckpointQueryProjectable`: whether a logged update's contribution to
  a later range query is computable from the action and overlap length
  alone; the property the checkpoint baseline's query needs.

They exist so a policy-generic structure can reject an unsupported policy at
compile time. The compiler propagates these facts; it does not prove them.
The evidence behind each value is section 2.1's tests and, eventually, the
reviewed theorems.

## 4. Strategy capability matrix

Sources audited at the commit introducing this file. "Action class" is the
class the strategy can support after generalization, with the stated proof
obligation; no strategy is claimed beyond what its mechanism preserves.

| Strategy | Source | Mechanism (audited) | Action class | Why / obligation |
| --- | --- | --- | --- | --- |
| Subject: tag-retaining persistent lazy tree | `include/valseg/persistent_lazy_segment_tree.hpp`, `src/persistent_lazy_segment_tree.cpp` | Path copying; the copied node keeps its lazy tag (`Node::lazy`, included in own `sum`, not in children); queries accumulate ancestor tags outermost-first (`query`'s `inheritedLazy`) | Policies with commuting induced actions | Accumulation reverses chronological order; see section 2.2. Obligation: sufficiency/necessity theorem. |
| Copy-on-push ablation | `bench/copy_on_push_segment_tree.hpp` | Same node layout and invariant, but an update descending past a tagged node pushes the tag into copied children first | Arbitrary valid action monoids | Push-down preserves chronological order at every node. Obligation: strategy-specific proof in the theory PR. |
| Ordinary lazy tree (control) | `include/valseg/lazy_segment_tree.hpp` | In-place push-down; latest state only | Arbitrary valid action monoids; no history | Textbook lazy propagation; chronological order preserved. |
| Point-only persistence | `include/valseg/point_only_persistent_segment_tree.hpp` | Path-copies to every affected leaf; no tags | Arbitrary valid action monoids | Actions are fully materialized at leaves in order; update cost grows with range width. |
| Full copy | `include/valseg/full_copy_persistent_segment_tree.hpp` | Complete tagless tree copy per update | Arbitrary valid action monoids | Every version is a materialized tree; no deferred state at all. |
| Buffered path copying | `include/valseg/buffered_path_copying_segment_tree.hpp` | Retains sum and lazy deltas in a per-node version-stamped buffer; path-copies on overflow | Audited claim only: same retained-tag accumulation as the subject, so at most the subject's class | Obligation: strategy-specific audit/proof before any broader claim. |
| Fat nodes | `include/valseg/fat_node_persistent_segment_tree.hpp` | Versioned `{version, children, sum, lazy}` states in-node (capacity 3); copies on overflow | Audited claim only: retains tags across versions, so at most the subject's class | Same obligation as buffered. |
| Checkpoint plus log | `include/valseg/checkpointing_segment_tree.hpp` | Full checkpoint tree every `K` versions plus an update log; a historical query reads the nearest checkpoint at or before the version and adds each later logged update's projected contribution | SumAdd and separately proved query-projectable policies only | See section 5. |

## 5. The checkpoint restriction

Checkpoint replay is not generic merely because log entries could store any
`Action`. The implemented query projects each logged range-add into a
range-sum by overlap length, delta times overlap length, which is exactly the
`kCheckpointQueryProjectable` property, and it holds for SumAdd because sums
are linear in their elements.

For a non-projectable pair such as MinAdd under partially overlapping
updates, a historical range-minimum cannot be assembled from
`(action, overlap length)` contributions; the query must reconstruct state
from the checkpoint by replaying updates into a scratch structure, or the
structure must retain more per version. A generic materialized variant would
pay `O(K)` replay work and `O(n)` temporary space per historical query, and
any comparison including it must report those costs, not the SumAdd
projection's. This plan does not implement that variant; the checkpoint
baseline stays in SumAdd-only comparisons.

## 6. Representation model R

The model in which the lower-bound attempt (theory PR, Gate G2) will be
stated. Fixed here, before the attempt, so it cannot drift to rescue a
result:

- the array has a fixed canonical binary partition (the segment-tree
  partition of `[0, n-1]` used by every implementation here);
- a version is identified by a root record; all records are immutable once
  a root that reaches them is published;
- an internal record carries the aggregate/tag state its subtree needs so
  that historical range queries are allocation-free (no scratch structures,
  no replay);
- updates act on the latest version only; queries read any published
  version;
- there is no hidden mutable side index and no global recomputation;
- the minimized unit is newly allocated records per update (with bytes as a
  layout-level refinement), not abstract RAM operations.

If a counterexample beats the subject's frontier inside R, the claim is
downgraded to a characterization; R is refined only with an independently
justified reason, never after the fact to preserve optimality.

## 7. What this document does not claim

- No generic checkpoint replay.
- No `O(log n)` bound for any version-stamped structure beyond what its own
  header documents.
- No correctness boundary theorem yet; sufficiency and necessity are
  obligations with named assumptions, not results.
- No claim that two policy instantiations prove generic correctness.
