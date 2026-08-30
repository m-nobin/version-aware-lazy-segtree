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
defined arithmetic are implementation assumptions, not algebraic laws. For an
integer policy, the algebra lives over mathematical integers while a concrete
machine type is a refinement: an execution is admissible only while every
aggregate, action and intermediate result is representable. The C++ policy
witnesses reject a non-admissible operation with `std::overflow_error`; no
policy silently changes to modular arithmetic.

The composition convention is fixed as `compose(newer, older)`: the older
action acts first. Every diagram, proof and implementation uses this
direction; `include/valseg/policy.hpp` documents it at the definition site.

### 2.1 Instantiations

| Policy | Definition | Arithmetic | Role |
| --- | --- | --- | --- |
| `SumAddPolicy` | ordered sum; additive actions; `apply(d, s, len) = s + d*len` | mathematical integers; the `long long` witness throws before an unrepresentable result | the full performance instantiation; what every existing structure hard-codes on its documented representable-input domain |
| `MinAddPolicy` | ordered minimum; additive actions; `apply(d, m, len) = m + d` for `len > 0` | mathematical integers; the `long long` witness throws before an unrepresentable result | structural and implementation-generality check |
| `AffineSumModPolicy<p>` | `x -> a*x + b` mod `p`; `apply((a,b), s, len) = a*s + b*len` | modular, laws exact for all inputs | minimal noncommutative witness; positive control for order-preserving strategies |

`tests/policy_test.cpp` checks every law exhaustively over small admissible
domains (and exhaustively over all actions and residues for
`AffineSumModPolicy<13>`), verifies the integer overflow rejection boundary,
exercises an affine modulus close to `2^32`, verifies that the induced
transformations of SumAdd and MinAdd commute on the tested domains, and pins
the minimal AffineSum counterexample: with older action `x -> x + 1` and newer
`x -> 2x`, the two application orders give 2 and 1. Tests validate these
implementations, not generic theorems.

### 2.2 Observational action image

For length `len`, an action `f` induces the transformation
`rho_len(f)(x) = apply(f, x, len)` on valid aggregates. Two actions are
observationally equivalent when they induce the same transformation at every
valid length. Correctness of the tag-retaining subject depends on the image
`rho(A)`, not on the action representation. The subject applies the tags on a
root-to-leaf path outermost-first, and the tree order does not record which
tag is newer. Both chronological orders occur, on `n = 4`:

- `rangeAdd(0, 3, g)` then `rangeAdd(1, 2, f)`: the second update copies the
  root and retains `g` on it, then places `f` on leaf 1. The query applies
  `g` outside `f`, so it computes `rho(g)(rho(f)(x))` where the semantics
  require `rho(f)(rho(g)(x))`: reversed.
- `rangeAdd(1, 2, f)` then `rangeAdd(0, 3, g)`: leaf 1 carries `f`, the
  second update places `g` on the copied root. The query applies `g`
  outside `f`, which is the chronological order: correct.

So the misordered pairs are exactly (tag retained on an ancestor through a
partial descent, tag placed below it by that or a later descent); every
ancestor tag placed after its descendants' tags is applied in the right
order. The same reversal happens inside a node's own aggregate: the partial
descent recomputes `apply(retainedTag, combine(left', right'), len)` with
the newer child action inside. The subject agrees with the chronological
semantics exactly when the induced transformations commute pairwise on
reachable aggregates; the first trace is the seed for the theory PR's
minimal AffineSum counterexample on the tree itself.

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
| Subject: tag-retaining persistent lazy tree | `include/valseg/persistent_lazy_segment_tree.hpp`, `src/persistent_lazy_segment_tree.cpp` | Path copying; the copied node keeps its lazy tag (`Node::lazy`, included in own `sum`, not in children); queries accumulate ancestor tags outermost-first (`query`'s `inheritedLazy`) | Policies with commuting induced actions | A tag retained through a partial descent is applied outside newer tags below it; see section 2.2. Obligation: sufficiency/necessity theorem. |
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

- computation is on a word RAM with word size
  `w >= ceil(log2(n + U + 1))`, where `U` is the number of published updates;
  one record occupies `O(1)` words, and policy states and operations occupy
  `O(1)` words and time;
- the array has the fixed canonical binary segment-tree partition of
  `[0, n-1]`; each tree record represents exactly one canonical interval and
  contains `O(1)` aggregate/action state plus two child references (or a leaf
  marker). A record may be addressed by a pointer or an integer arena index;
  packing several canonical nodes into one variable-size record is outside R;
- a version table stores one root handle per published version. The handle is
  counted and reported separately as the unavoidable `Theta(1)` publication
  cost; “records per update” below means newly allocated tree records, not this
  handle or the one-time version-0 build;
- records reachable from a published root are immutable. A new version may
  reuse any existing immutable record by reference, including a whole
  unchanged subtree, but cannot mutate a record reachable from an older root;
- updates are online, apply only to the latest version and must finish in
  `O(log n)` worst-case time. Historical interval queries may read any
  published root, allocate nothing and must finish in `O(log n)` worst-case
  time using only the root and records reachable from it;
- there is no replay log, checkpoint reconstruction, mutable side index,
  parent-pointer repair, global recomputation or uncounted content-addressed
  interning table. An auxiliary persistent structure is permitted only if its
  records, roots, update time and query probes are included in the same cost;
- arithmetic follows section 2's exact mathematical policies; a machine run
  outside its representable refinement is not an input on which a bound is
  claimed;
- the target quantity is the worst-case number of new tree records needed by
  one nonempty range action after an arbitrary valid history. The theory PR
  must also state the corresponding total over an update sequence and must not
  silently substitute an amortized or expected bound. Bytes are a separately
  reported layout refinement, not the structural unit.

R deliberately excludes the checkpoint and full-copy engineering baselines
from the lower-bound comparison when they violate its query-time or record
shape requirements. They remain valid empirical controls.

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
