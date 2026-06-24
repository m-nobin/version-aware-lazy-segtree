# Literature Review: Segment Trees: Simple, Dynamic, and Persistent

## Paper Information

**Title:** Segment Trees: Simple, Dynamic, and Persistent

**Author:** M. Besher Massri

**Paper Link:**
https://salomon.ijs.si/Seminars/segment.tree.pdf

---

## Summary

Massri presents a comprehensive overview of the Segment Tree data structure and its major variants, including:

1. Simple Segment Trees
2. Dynamic Segment Trees
3. Persistent Segment Trees

The paper explains how segment trees efficiently support range queries and updates by storing precomputed information over intervals of an array. It also discusses how persistence can be incorporated into segment trees to preserve historical versions while maintaining efficient query and update operations.

---

## Key Contributions

### Simple Segment Trees

The paper introduces the standard segment tree structure, where:

- Each node represents a range of elements.
- Internal nodes store aggregated information from their children.
- Common operations include:
  - Range Sum Query
  - Range Minimum Query
  - Range Maximum Query

The height of the tree is:
```math
O(\log n)
```

which enables efficient query and update operations.

---

### Dynamic Segment Trees

The paper discusses dynamic segment trees, which allocate nodes only when needed.

Benefits:

- Supports very large coordinate ranges.
- Reduces memory usage for sparse datasets.
- Useful when the array size is unknown or extremely large.

---

### Persistent Segment Trees

The paper explains how persistence can be applied to segment trees by preserving previous versions after updates.

Instead of rebuilding the entire tree:

- Only nodes on the update path are copied.
- Unchanged nodes are shared between versions.
- Historical versions remain accessible.

This technique is based on path-copying persistence.

---

## Complexity Analysis

### Standard Segment Tree

| Operation | Complexity |
|------------|------------|
| Build | O(n) |
| Query | O(log n) |
| Update | O(log n) |
| Memory | O(n) |

---

### Persistent Segment Tree

| Operation | Complexity |
|------------|------------|
| Query | O(log n) |
| Update | O(log n) |
| Extra Memory per Update | O(log n) |
| Historical Query Support | Yes |

Only the nodes along the root-to-leaf update path are copied, resulting in logarithmic additional memory per version.

---

## Relevance to Our Project

This paper is directly relevant because it provides the foundation for persistent segment trees, which form the core data structure used in our research. The paper demonstrates how path-copying can preserve historical versions efficiently while maintaining logarithmic query and update complexity. However, the persistent segment tree discussed in the paper focuses primarily on preserving node values and does not investigate version-aware management of lazy propagation information.

---

## Research Gap

### What This Paper Addresses

- Segment Trees
- Dynamic Segment Trees
- Persistent Segment Trees
- Path Copying
- Historical Queries

### What It Does Not Address

- Version-Aware Lazy Propagation
- Persistent Lazy Tags

---

## Connection to Our Research

Our project builds upon the persistent segment tree framework described in this paper.

While the paper uses path copying to preserve historical versions of node values, we investigate whether lazy propagation information can also be managed in a version-aware manner without violating correctness or significantly increasing time and space complexity.

---