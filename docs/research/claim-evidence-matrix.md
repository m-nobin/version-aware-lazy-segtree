# Claim-evidence matrix

Prior-art audit for the research programme, originally recorded 30 August
2026 and re-audited against the retained primary sources on 31 August 2026.
Search-result counts are not novelty evidence. Every retained claim below has
an exact locator in section 7a, and every proposed contribution names the
closest precedent found in the audited corpus and the narrower difference the
project can test or defend.

The project's proposed contributions are:

- **C1** semantic model and strategy capability taxonomy
  ([capability-taxonomy.md](capability-taxonomy.md));
- **C2** observational commutativity boundary for retained, outermost-first
  action accumulation;
- **C3** exact visited/push frontier laws in a stated representation model;
- **C4** physical and predictive cost models; and
- **C5** registered, externally validated regime study and reusable artifact.

## 1. Persistence foundations

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Driscoll, Sarnak, Sleator, Tarjan, "Making data structures persistent", JCSS 38(1):86-124, 1989, DOI 10.1016/0022-0000(89)90034-2 | Gives fat-node and node-copying transformations for partial persistence and node splitting for full persistence in a pointer-machine model; updates are local field changes. | Foundation for the persistence taxonomy, but it does not model range actions, deferred aggregate state or lazy tags. |
| Sarnak, Tarjan, "Planar point location using persistent search trees", CACM 29(7):669-679, 1986, DOI 10.1145/6138.6151 | Uses path copying and limited node copying for persistent search trees; queries and updates take logarithmic time and the improved representation uses constant amortized space per update. | Establishes a path-copying baseline, but has no range updates or aggregates. |
| Becker et al., "An asymptotically optimal multiversion B-tree", VLDB Journal 5(4):264-275, 1996, DOI 10.1007/s007780050028 | External-memory MVBT with point insertion/deletion at the current version and queries at historical versions; matches single-version B-tree I/O bounds with space linear in updates. | Point-update, external-memory index with no aggregation. Its stated model illustrates why C3 must be scoped to a specific representation. |
| Fiat, Kaplan, "Making data structures confluently persistent", J. Algorithms 48(1):16-58, 2003, DOI 10.1016/S0196-6774(03)00044-0 | Defines confluent persistence with version-DAG merges and proves information-theoretic space-expansion bounds in that model. | C3 concerns partial persistence with range actions rather than confluent point-update transformations. The earlier lower-bound attempt was withdrawn after a counterexample in the project's own model. |

## 2. Lazy segment-tree invariants

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Ibtehaz, Kaykobad, Rahman, "Multidimensional segment trees can do range updates in poly-logarithmic time", TCS 854:30-43, 2021; primary [arXiv:1811.01226](https://arxiv.org/abs/1811.01226) | Emulates lazy propagation in higher dimensions using dispersed/intended updates and partial/complete queries, proves the range-sum construction, and derives `O(log^d n)` update/query time. It describes extension only to aggregates compatible with repeated combination. | Orthogonal dimensionality result. The manuscript contains no persistence model and no retained-tag action-order boundary. |
| AtCoder Library, [`lazy_segtree`](https://atcoder.github.io/ac-library/production/document_en/lazysegtree.html) | Specifies a monoid, mapping, identity and action composition. In-place propagation preserves action order; commutativity is not required. | The subject removes push-down for retained ancestor tags. C2 states the additional algebraic condition needed by that representation; ordinary lazy propagation is the arbitrary-action control. |

## 3. Temporal aggregate indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Yang, Widom, "Incremental computation and maintenance of temporal aggregates", VLDB Journal 12(3):262-283, 2003, DOI [10.1007/s00778-003-0107-z](https://doi.org/10.1007/s00778-003-0107-z) (SB-tree) | Interior records retain partial aggregate contributions; lookup accumulates along a path and interval insertion stores a fully covered contribution at the highest possible level. The paper treats SUM/COUNT/AVG and separate min/max variants. The index is mutable, not a set of published persistent roots. | Strong precedent for retained deferred aggregation. The narrower difference is immutable copy-on-write versions and the resulting cross-version tag-order and record-frontier questions. |
| Zhang et al., "On computing temporal aggregates with range predicates", TODS 33(2), 2008, DOI [10.1145/1366102.1366109](https://doi.org/10.1145/1366102.1366109) (MVSB-tree); primary [author manuscript](https://www.cs.ucr.edu/~tsotras/functional/1.pdf) | Makes an SB-tree partially persistent under transaction-time order, copies/splits disk pages, and reduces key/time rectangle SUM to four dominance-sum queries over two MVSB-trees. It proves logarithmic query/update I/O bounds and evaluates one million intervals. | Closest database-side precedent for persistence plus retained aggregate contributions. It studies ordered point insertions, external-memory page multiversioning and fixed-size additive values, not in-memory range actions over a fixed canonical partition or immutable-record frontiers. |
| Salzberg, Tsotras, "Comparison of access methods for time-evolving data", ACM Computing Surveys 31(2):158-221, 1999, DOI 10.1145/319806.319816 | Analytical taxonomy and asymptotic comparison of transaction-time and valid-time access methods; no measured study of range aggregates. | Terminology and comparison-framework anchor; it does not cover C2-C5. |

## 4. Experimental persistence

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Pluquet et al., "Implementing partial persistence in object-oriented languages", ALENEX 2008, DOI [10.1137/1.9781611972887.4](https://doi.org/10.1137/1.9781611972887.4); primary [author PDF](https://fpluquet.be/Publications_%26_Talks_files/Implementing%20Partial%20Persistence%20in%20Object-Oriented%20Languages.pdf) | Presents transparent fat-node-style partial persistence in Java/AspectJ and reports controlled single-machine timing and memory experiments, including state storage, instrumentation layers, snapshot frequency, treaps and point location. | Establishes in-memory experimental persistence for field updates and object graphs. C5's narrower difference is a registered paired cross-machine regime study of range-action aggregate structures. |
| Pluquet, Langerman, Wuyts, "Executing code in the past: efficient in-memory object graph versioning", OOPSLA 2009, DOI 10.1145/1640089.1640118 (HistOOry) | Gives three in-memory object-versioning primitives, snapshot access and three application studies, with performance and memory measurements. | Close systems and methodology precedent, but no range actions, aggregate algebra or strategy-selection model. |
| Kaplan, "Persistent data structures", in *Handbook of Data Structures and Applications*, chapter 31, 2004; primary [chapter PDF](https://bjpcjp.github.io/pdfs/math/other-datastructs-DSA.pdf) | Surveys partial, full and confluent persistence and the fat-node, path-copying, node-copying and node-splitting techniques with their time/space tradeoffs. | Comparative technique taxonomy, not a measured range-aggregate study. C1 refines this family along lazy-state and algebraic-capability axes. |

## 5. Recent buffered and multiversion indexes

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Brodal, Rysgaard, Svenning, "External memory fully persistent search trees", STOC 2023, DOI 10.1145/3564246.3585140; primary [author PDF](https://cs.au.dk/~gerth/papers/stoc23.pdf) | Gives a fully persistent external-memory search tree with B-tree query/update I/O bounds and space linear in updates. | Point-update dictionary with no aggregates, tags or range actions. |
| Brodal, Rysgaard, Svenning, "Buffered partially-persistent external-memory search trees", ESA 2025, DOI 10.4230/LIPIcs.ESA.2025.82; primary [LIPIcs PDF](https://drops.dagstuhl.de/storage/00lipics/lipics-vol351-esa2025/LIPIcs.ESA.2025.82/LIPIcs.ESA.2025.82.pdf) | Gives a partially persistent buffered external-memory search tree for point insertion/deletion, with linear space and the paper's stated query/update I/O bounds. | Buffering batches point updates for I/O; it does not retain range actions as queryable deferred aggregate state. |
| Kaufmann et al., "Timeline index: a unified data structure for processing queries on temporal data in SAP HANA", SIGMOD 2013, DOI 10.1145/2463676.2465293; primary [author-uploaded text](https://www.researchgate.net/publication/243961981_Timeline_Index_A_Unified_Data_Structure_for_Processing_Queries_on_Temporal_Data_in_SAP_HANA) | Uses an Event List plus a Version Map; optional selected-version checkpoints materialize full bit vectors. It supports time travel, joins and temporal aggregates including SUM/AVG/COUNT and MIN/MAX/MEDIAN, and appends events incrementally. | Closest same-purpose in-memory checkpoint/log baseline. It does not publish immutable per-update segment-tree records or analyze retained ancestor-tag order/frontiers. Its aggregate scope is broader than additive-only. |

## 6. Direct implementation precedents

| Work | Verified facts | Difference this project defends |
| --- | --- | --- |
| Library Checker, `persistent_range_affine_range_sum` | Source audited at fixed commit [`04c8de3`](https://github.com/yosupo06/library-checker-problems/commit/04c8de378bab67be926325de2871f0babb8e6451). `data_structure/persistent_range_affine_range_sum/sol/correct.cpp` defines `PersistentLazySegtree`; partial updates accumulate ancestor composition and call `applyAtNode` on copied child roots. | Copy-on-push precedent for branching persistent affine actions. It materializes accumulated tags before descent rather than retaining an ancestor tag over newly changed descendants. |
| ei1333 library, `structure/bbst/persistent-lazy-red-black-tree.hpp` | `propagate` clones children while pushing lazy state in a persistent balanced tree. | Engineering precedent for the copy-on-push ablation, not the retained-tag subject. |
| cp-algorithms, [segment tree](https://cp-algorithms.com/data_structures/segment_tree.html) | The section "Preserving the history of its values" presents point-update persistence; lazy propagation is presented separately. | Tutorial precedent does not combine persistent versions with retained lazy range actions. |

## 7. Empirical methodology

| Work | Verified facts | Use |
| --- | --- | --- |
| SIGPLAN [Empirical Evaluation Guidelines](https://www.sigplan.org/Resources/EmpiricalEvaluation/) and checklist, version dated 26 October 2018 | Checklist categories cover claims, comparisons, benchmark choice, data analysis, metrics, experimental design and presentation. | Design standard for C5; cite Blackburn as checklist chair. |
| ACM [Artifact Review and Badging](https://www.acm.org/publications/policies/artifact-review-and-badging-current), version 1.1, 24 August 2020 | Defines Artifacts Available; Artifacts Evaluated--Functional/Reusable; and Results Reproduced/Replicated. | Target vocabulary and evidence expectations for the final artifact. |
| Johnson, "A theoretician's guide to the experimental analysis of algorithms", DIMACS Implementation Challenges volume, 2002, pp. 215-250; primary [author manuscript](https://web.cs.dal.ca/~eem/gradResources/A-theoreticians-guide-to-experimental-analysis-of-algorithms-2001.pdf) | Ten principles include experimental design, reproducibility, suitable comparisons, variance reporting and machine/compiler dependence. | Experimental-design standard for C5. |
| McGeoch, [*A Guide to Experimental Algorithmics*](https://www.cambridge.org/core/books/guide-to-experimental-algorithmics/), Cambridge University Press, 2012 | Treats measurement, experiment design, variance reduction (including paired/common-random-number designs) and analysis. | Basis for paired scheduling, blocking and variance-reduction choices in the confirmatory protocol. |

## 7a. Primary-source locator index

These locators identify the passages used for the retained claims. Page numbers
are printed publication pages unless explicitly marked PDF/manuscript pages.

| Source | Exact locator | Claim checked |
| --- | --- | --- |
| DSST 1989 | §§2-3, pp. 92-108 | Fat nodes/node copying for partial persistence; node splitting for full persistence; transformation costs |
| Sarnak-Tarjan 1986 | §§2-3, pp. 671-676 | Path copying, limited node copying and stated search/update/space bounds |
| MVBT 1996 | §§1-3, pp. 264-270 | Versioned interface, page structure and asymptotic analysis |
| Fiat-Kaplan 2003 | §1.1, pp. 18-20; Table 1, p. 22; §2 and Theorem 2.1, pp. 25-28 | Confluent model and lower-bound scope |
| Ibtehaz et al. | §§4.1-4.3, manuscript pp. 5-15; Theorems 4.6-4.8, pp. 12-13; §5.1, pp. 15-16 | Deferred-update mechanisms, correctness, `O(log^d n)`, aggregate restriction; no persistence model |
| AtCoder `lazy_segtree` | "Properties" and constructor/API specification | Monoid/action laws and composition interface |
| SB-tree | §3, pp. 264-271, especially §§3.1 and 3.3; §§4.2-4.3; Table 8 | Lookup/update mechanism, additive and min/max scope, costs |
| MVSB-tree | Manuscript §§3.1-3.3, PDF pp. 8-11; §4, pp. 12-22; Corollary 1, p. 22; §6.1, pp. 26-27 | Four-query reduction, page multiversioning, bounds and experiment scale |
| Salzberg-Tsotras 1999 | Abstract and §1, pp. 158-161; §3, pp. 167-174; §5, pp. 185-214 | Analytic comparison criteria and taxonomy |
| Pluquet et al. 2008 | §§2-4, pp. 38-43; §5, pp. 43-46 | Mechanism, AspectJ implementation, machine/runtime controls and measured comparisons |
| HistOOry 2009 | Abstract and §1, pp. 391-392; §§4.1-4.2, pp. 394-398; §§5-6, pp. 399-405 | Primitives, representation, applications and measurements |
| Kaplan 2004 | §31.1, pp. 31-1--31-3; p. 31-4; §31.3, pp. 31-9 onward | Persistence models, path copying and general transformation taxonomy |
| Brodal et al. 2023 | Abstract and §1.1, pp. 1410-1411 | Fully persistent interface and stated I/O/space bounds |
| Brodal et al. 2025 | Abstract and §1.2, pp. 82:1-82:3; Theorem 1, pp. 82:4-82:5; §2 | Point-update semantics, bounds and path-copying implementation |
| Timeline Index | §§4.2-4.4; §§5.1.1-5.1.2 | Event List/Version Map, optional checkpoints, incremental append and full aggregate scope |
| Library Checker | Fixed commit above; `correct.cpp`: class `PersistentLazySegtree`, recursive update and `applyAtNode` | Copy-on-push behavior and affine-action scope |
| ei1333 | `persistent-lazy-red-black-tree.hpp`: `propagate` | Child cloning during lazy propagation |
| cp-algorithms | "Lazy Propagation" and "Preserving the history of its values" | Lazy and persistent mechanisms are separate; persistent example is point-update |
| SIGPLAN checklist | Seven named checklist categories in `checklist/checklist.yml` | Empirical-review criteria |
| ACM badging v1.1 | Headings "Artifacts Available", "Artifacts Evaluated" and "Results Validated" | Badge names and evidence meanings |
| Johnson 2002 | Ten principles, manuscript pp. 2-3; Principle 4, p. 11; Principle 6, pp. 14-15; Principle 7, pp. 18-20 | Design, reproducibility and platform/comparison reporting |
| McGeoch 2012 | Ch. 2, pp. 17-49; Ch. 3, pp. 50-97; §6.1, pp. 184-214, especially p. 190; Ch. 7, pp. 215-256 | Measurement, design, paired variance reduction and analysis |

## 7b. Audit fields for the closest items

| Item | Semantic operation model | Persistence model | Memory model | Algebraic scope | Bounds | Measured evidence | Artifact | Exact difference defended |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DSST 1989 | Point field updates | Partial and full | Pointer machine | None | Transformation bounds per field change | None | Theory paper | No range actions or deferred aggregate state |
| SB-tree | Interval insertion; temporal aggregate lookup | One mutable index | External-memory pages | SUM/COUNT/AVG; min/max variants | Logarithmic lookup/update | Journal experiments | No released code/data | No immutable roots or cross-version tag order |
| MVSB-tree | Ordered point insertion; key/time rectangle SUM | Partial transaction time | External memory | Fixed-size additive values | Logarithmic I/O bounds; stated space bound | One-million-interval study | No released code/data | Different operation and page representation; no retained range-action frontier |
| Pluquet et al. 2008 | Object-field updates | Partial, fat-node style | JVM heap | None | DSST-derived transformation | Controlled single-machine study | No released artifact | No range actions/aggregates or registered cross-machine design |
| Library Checker | Range affine update/sum; branch any version | Branching roots | Immutable pooled nodes | Affine mod 998244353 | `O(log n)` per operation | Judge only | Fixed public source | Copy-on-push rather than retained ancestor tags |

## 8. Verdict by contribution

| Contribution | Closest precedent | Status after audit |
| --- | --- | --- |
| C1 taxonomy | Kaplan and DSST technique taxonomies; Salzberg-Tsotras comparison framework | Open. The audited corpus did not contain one comparison that places retained tags, copy-on-push, materialization, snapshots/checkpoints, modification boxes and fat nodes under the project's algebraic interface. |
| C2 boundary | MVSB/SB additive contributions; Library Checker copy-on-push affine implementation | Established at G2 for the stated representation. `docs/proof.md` §9 gives sufficiency, conditional necessity, faithful-action corollary and an AffineSum witness. |
| C3 frontier laws | DSST/MVBT point-update accounting; Fiat-Kaplan model-specific lower bounds | Exact frontier identities established at G2. The lower-bound attempt was withdrawn after the edge-tag counterexample, so no project optimality claim remains. |
| C4 predictive cost model | No source in the audited corpus provided a directly comparable persistence-strategy selection model | Model form and units are frozen; predictive success remains unestablished until the registered holdout. |
| C5 registered regime study | Pluquet et al. 2008 and HistOOry 2009 | Open. The proposed distinction is a registered, paired, cross-machine range-action study, not experimental persistence in general. |

## 9. Gate G1 technical assessment (corrected 31 August 2026)

- The four previously unresolved primary-source flags (Kaplan, Timeline Index,
  Johnson and McGeoch) are resolved; all other retained rows were rechecked and
  section 7a records exact locators.
- The Timeline Index description is corrected: it uses an Event List and
  Version Map, checkpoints are optional, and its aggregate scope is not merely
  additive.
- Every contribution is paired with a closest precedent and a scoped
  difference. The matrix makes no universal novelty, empty-intersection or
  project optimality claim.
- The pilot is preserved as exploratory evidence with a 96-file checksum
  manifest and a one-command reproduction entry point.
- The algebraic laws, composition direction and generic checkpoint restriction
  remain recorded in `policy.hpp` and `capability-taxonomy.md`.

Technical recommendation: **continue Route B with the narrow scopes in section
8.** This is an evidence assessment, not formal Gate G1 approval. Gate G1 also
requires durable approval from an independent human reviewer under the Phase 1
charter, and that governance condition is still pending.

## 10. Independent review evidence audit

The repository and GitHub records were checked on 31 August 2026. Pull requests
#34 and #35 contain no submitted reviews or review comments. Their merge and
closeout commits prove delivery, not independent approval. The earlier phrase
"recorded by the repository owner" is therefore withdrawn as insufficient
evidence and is not replaced by another self-attestation.

| Field | Current verifiable record |
| --- | --- |
| Reported reviewer | Sunjare Zulfiker |
| Reported independence basis | Described in the prior record as a second contributor who did not author the Phase 1 changes; not yet corroborated by a durable reviewer-authored record |
| Durable approval reference | **Pending** — no PR review, signed document or archived reviewer message is currently linked |
| Decision | Technical recommendation is Route B; formal reviewer decision remains unverified |
| Required changes and dispositions | **Pending reviewer record.** It must state either no changes, or list each requested change and its disposition |
| Acceptable closure evidence | A submitted PR review, reviewer-signed document, or archived reviewer email/message that identifies the reviewed material, decision, date, independence basis and required-change dispositions |

The separate [AI technical review](phase-1-ai-review.md) verified the source
audit and pilot reproduction and identified three corrective actions. Its
confirmatory metadata guard and stale-record findings have been resolved in
the working copies; public Wiki publication remains pending. That AI review is
additional technical evidence, not a human signature.

Until that evidence is added, Phase 1 is technically closed but its external
governance condition is open, and Gate G1 must not be represented as formally
reconfirmed.
