# Ordinary Lazy Segment Tree Baseline

## Overview

This module implements a standard **Lazy Propagation Segment Tree** that supports efficient **range addition updates** and **range sum queries**.

Unlike the brute-force versioned array, this implementation **does not support persistence**. All updates modify the current tree directly, making it suitable as a **performance baseline** for comparison with the proposed partially persistent segment tree.

---

# Objectives

The primary objectives of this implementation are:

- Provide an efficient baseline for range updates and queries.
- Demonstrate the benefits of lazy propagation.
- Serve as the performance benchmark for the proposed persistent data structure.
- Validate correctness through deterministic test cases.

---

# Features

- Segment tree construction
- Lazy propagation
- Range addition updates
- Range sum queries
- Recursive build, update, query, and push operations
- Input validation
- Error handling

---

# Data Structure

Each node stores:

- **sum** — the sum of the corresponding segment.
- **lazy** — a deferred update value that has not yet been propagated to the node's children.

The implementation uses the classical **array-based representation** of a segment tree.

Example:

```text
                 0
              /     \
             1       2
           /  \     /  \
          3    4   5    6
```

The arrays are:

```cpp
tree[]
lazy[]
```

---

# Lazy Propagation

Instead of immediately updating every element in a range, the update is stored as a **lazy value** at the affected node.

The update is propagated to child nodes only when necessary during a future update or query operation.

This avoids repeatedly visiting every element in the updated range.

Example:

```text
Update [2, 6] +5

Instead of updating every element individually,

Store:

lazy[node] += 5

Later, when the children are visited,

push() propagates the pending update.
```

---

# Public Interface

The implementation provides the following operations:

| Function | Description |
|----------|-------------|
| `initialize()` | Build the segment tree from an input array. |
| `rangeAdd(left, right, value)` | Add a value to every element in the specified range. |
| `rangeSum(left, right)` | Compute the sum of elements within the specified range. |
| `size()` | Return the number of elements in the array. |

---

# Complexity Analysis

| Operation | Complexity |
|-----------|------------|
| Build | O(n) |
| Range Update | O(log n) |
| Range Query | O(log n) |
| Memory | O(n) |

---

# Limitations

This implementation is **not persistent**.

Every update modifies the current tree directly, meaning previous states are permanently overwritten.

Therefore, historical queries such as

```text
Query Version 0
```

are **not supported**.

---

# Purpose in This Research

This implementation serves as the **performance baseline**.

It demonstrates the efficiency of classical lazy propagation without persistence.

Later, the proposed **Version-Aware Lazy Propagation for Partially Persistent Segment Trees** will be compared against this implementation to evaluate:

- Update performance
- Query performance
- Memory overhead introduced by persistence

---

# Project Structure

```text
lazy_segment_tree/

├── lazy_segment_tree.hpp
├── lazy_segment_tree.cpp
└── README.md
```

---

# Verification

The implementation has been verified using deterministic test cases covering:

- Initialization
- Single range updates
- Multiple updates
- Whole array updates
- Single element updates
- Negative updates
- Overlapping updates
- Multiple range queries

All deterministic tests currently pass successfully.