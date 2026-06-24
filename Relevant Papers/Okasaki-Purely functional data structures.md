# Literature Review: Purely Functional Data Structures (1996/1998)

## Paper Information

**Title:** Purely Functional Data Structures

**Author:** Chris Okasaki

**Publication:**
- CMU-CS-96-177 (PhD Thesis, 1996)
- Cambridge University Press (Book, 1998)

**Paper Link:**
https://www.cs.cmu.edu/~rwh/students/okasaki.pdf

---

## Summary

Okasaki's work is one of the most influential contributions to functional programming and persistent data structures. The book demonstrates how efficient data structures can be designed in a purely functional setting, where existing structures are never modified in place. Instead, updates create new versions while preserving access to older versions.

A key observation made by Okasaki is that purely functional data structures are naturally persistent because updates create new structures rather than overwriting existing ones. Unchanged portions of a structure can be safely shared among multiple versions, reducing memory overhead while preserving historical access.

---

## Key Contributions

### Persistence Through Immutability

Instead of destructive updates:

```text
Old Version
    ↓
Modified In Place
```

functional data structures use:

```text
Old Version
     \
      \
   New Version
```

Both versions remain accessible simultaneously.

This naturally provides persistence without requiring explicit version-management mechanisms.

---

### Structural Sharing

When a structure is updated:

- Only affected nodes are recreated.
- Unchanged nodes are shared.
- Multiple versions coexist efficiently.

Example:

```text
Version 0

      A
     / \
    B   C
```

After updating B:

```text
Version 1

      A'
     /  \
    B'   C
```

Node C is shared between versions.

This idea is conceptually similar to path-copying persistence.

---

### Lazy Evaluation and Persistence

One of Okasaki's major contributions is showing how lazy evaluation can be combined with persistent data structures.

The book develops techniques for:

- Deferred computation
- Amortized analysis
- Persistent data structures
- Efficient functional implementations

These ideas later influenced research on immutable and versioned data structures.

---

## Complexity Perspective

The book does not focus on a single data structure and therefore does not provide one universal complexity table.

Instead, Okasaki demonstrates that many classical data structures can achieve asymptotic complexities comparable to their imperative counterparts while remaining persistent.

Examples discussed include:

| Data Structure | Typical Complexity |
|----------------|--------------------|
| Stack | O(1) |
| Queue | O(1) amortized |
| Search Tree | O(log n) |
| Priority Queue | O(log n) |

The key insight is that persistence can often be achieved without fundamentally changing asymptotic complexity.

---

## Relevance to Our Project

This work provides the conceptual foundation for persistence through immutability and structural sharing.

Our project focuses on partially persistent segment trees, which also preserve previous versions while sharing unchanged structure across updates.

The ideas of:

- Structural sharing
- Version preservation
- Non-destructive updates

are directly relevant to the design of version-aware lazy propagation.

---

## Research Gap

### What This Book Addresses

- Persistent data structures
- Structural sharing
- Functional persistence
- Lazy evaluation
- Amortized analysis

### What It Does Not Address

- Segment trees
- Persistent segment trees
- Lazy propagation for range updates
- Version-aware lazy tags
- Historical range-query correctness

---

## Connection to Our Research

Okasaki demonstrates that persistence can be achieved naturally through immutable structures and structural sharing. Our work applies similar persistence principles to segment trees while addressing an additional challenge: How can deferred updates (lazy propagation) be managed correctly across multiple historical versions? Unlike the data structures studied by Okasaki, our research focuses specifically on range-update data structures and version-aware lazy tag management.

---