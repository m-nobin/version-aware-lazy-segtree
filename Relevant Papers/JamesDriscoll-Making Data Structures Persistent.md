# Literature Review: Making Data Structures Persistent (1989)

## Paper Information

**Title:** Making Data Structures Persistent

**Authors:**
- James R. Driscoll
- Robert E. Tarjan
- Daniel D. Sleator
- Neil Sarnak

**Paper Link:**
https://www.cs.cmu.edu/~sleator/papers/another-persistence.pdf

---

## Summary

Driscoll et al. (1989) introduced the concept of **persistence in data structures**, allowing access to previous versions of a data structure after updates.

The paper formally defined different persistence models:

### Partial Persistence
- Query any version.
- Update only the latest version.

### Full Persistence
- Query any version.
- Update any version.

The paper also introduced two fundamental techniques for implementing persistence:

1. Path Copying
2. Fat Nodes

These techniques became the foundation of many later persistent data structures, including persistent search trees, persistent segment trees, and functional data structures.

---

## Relevance to Our Project

This paper serves as the theoretical foundation of our research.

The original fat-node and path-copying techniques were designed for preserving historical versions under ordinary value updates.

Our project extends these ideas to support **version-aware lazy propagation**, where lazy tags are managed across multiple versions of a partially persistent segment tree.

---

# Persistence Techniques

## Path Copying

**Description:**

When an update occurs, create copies of only the nodes affected by the update. Unchanged nodes are shared between versions.

### Example

Version 0:

```text
      A
     / \
    B   C
```

Update node B.

Version 1:

```text
      A'
     /  \
    B'   C
```

Only the modified path is copied.

---

## Fat Nodes

**Description:**

Instead of copying nodes, store all historical values inside the same node along with their version numbers.

### Example

```text
Node B

----------------
Version | Value
----------------
0       | 5
1       | 10
2       | 20
----------------
```

The node stores its entire update history.

---

# Complexity Comparison

| Operation | Path Copying | Fat Nodes |
|------------|------------|------------|
| Update Time | O(log n) | O(log n) |
| Query Time | O(log n) | O(log n · log V) |
| Extra Memory per Update | O(log n) | O(1) to O(log n) |
| Historical Query Support | Yes | Yes |

Where:

- **n** = Number of elements
- **V** = Number of versions

---