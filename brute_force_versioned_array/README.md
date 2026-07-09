# Brute-Force Versioned Array

## Purpose

The **Brute-Force Versioned Array** is a baseline implementation developed for the research project:

> **Version-Aware Lazy Propagation for Partially Persistent Segment Trees: Design, Correctness, and Benchmarking**

Its primary purpose is **correctness**, not performance. Every update creates a complete copy of the previous array, preserving all historical versions. This implementation serves as the **correctness oracle** against which the proposed partially persistent segment tree will be validated.

---

## Motivation

Before implementing a complex persistent data structure, it is essential to have a simple implementation whose behavior is easy to verify.

Although copying the entire array after every update is computationally expensive, it provides a highly reliable reference implementation. During experimentation, identical operations will be executed on both the brute-force implementation and the proposed persistent segment tree. Matching outputs provide strong evidence that the proposed implementation is correct.

---

## Features

- Complete version history
- Immutable historical versions
- Range addition updates
- Range sum queries
- Deterministic behavior
- Simple implementation for debugging and validation

---

## Data Structure

Each version stores a complete copy of the array.

Example:

```text
Version 0
[1, 2, 3, 4]

↓

Range Add (+5 on indices 1–2)

↓

Version 1
[1, 7, 8, 4]

↓

Range Add (-2 on indices 0–3)

↓

Version 2
[-1, 5, 6, 2]
```

Older versions remain unchanged throughout the lifetime of the program.

---

## Public API

### Initialize

```cpp
initialize(initialArray);
```

Creates Version 0.

---

### Range Addition

```cpp
rangeAdd(version, left, right, value);
```

Creates a new version by copying the specified version and adding `value` to every element in the range `[left, right]`.

Returns the newly created version number.

---

### Range Sum

```cpp
rangeSum(version, left, right);
```

Returns the sum of elements within the specified range for the selected version.

---

### Version Count

```cpp
versionCount();
```

Returns the total number of stored versions.

---

### Get Version

```cpp
getVersion(version);
```

Returns a read-only reference to a stored version.

---

## Complexity Analysis

| Operation | Time Complexity | Space Complexity |
|------------|-----------------|------------------|
| Initialize | O(n) | O(n) |
| Create Version | O(n) | O(n) |
| Range Add | O(n) | O(n) |
| Range Sum | O(n) (worst case) | O(1) |
| Get Version | O(1) | O(1) |
| Version Count | O(1) | O(1) |

where **n** is the number of elements in the array.

---

## Advantages

- Extremely easy to verify
- Preserves every historical version
- Ideal as a correctness oracle
- Simple to debug
- Minimal implementation complexity

---

## Limitations

- Every update copies the entire array.
- Memory usage grows linearly with the number of versions.
- Range queries scan the requested range directly.
- Not suitable for large datasets or performance benchmarking.

---

## Role in This Research

This implementation is **not** the proposed solution.

Instead, it acts as the baseline correctness implementation for validating the proposed:

> **Version-Aware Lazy Propagation for Partially Persistent Segment Trees**

During testing:

1. A sequence of updates and queries is generated.
2. The same sequence is executed on:
   - Brute-Force Versioned Array
   - Proposed Persistent Segment Tree
3. Every query result is compared.
4. Any mismatch indicates an implementation error.

This differential testing strategy provides high confidence in the correctness of the proposed data structure.

---

## Future Integration

This implementation will later be used together with:

- Ordinary Lazy Segment Tree (performance baseline)
- Partially Persistent Segment Tree (proposed method)
- Random Operation Generator
- Benchmarking Framework

Together, these components will support correctness verification and experimental evaluation of the proposed research contribution.