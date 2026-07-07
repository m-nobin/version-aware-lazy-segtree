# Literature Review: Multidimensional Segment Trees Can Do Range Updates in Poly-Logarithmic Time

## Paper Information

**Title:** Multidimensional Segment Trees Can Do Range Updates in Poly-Logarithmic Time

**Authors:**
- Nabil Ibtehaz
- M. Kaykobad
- M. Sohel Rahman

**Paper Link:**
https://arxiv.org/abs/1811.01226

---

## Summary

Ibtehaz et al. revisit one of the long-standing challenges in computational geometry: efficiently performing **range updates** in multidimensional segment trees.

While one-dimensional segment trees with lazy propagation support both range updates and range queries in **O(log n)** time, extending these operations to higher dimensions is significantly more difficult. Existing multidimensional segment trees efficiently support range queries but cannot directly perform lazy propagation without incurring high complexity.

The authors propose a modified multidimensional segment tree that supports both range updates and range queries in **polylogarithmic time**, overcoming a major limitation of classical multidimensional segment trees.

---

## Key Contributions

### Efficient Multidimensional Range Updates

The paper introduces a modified multidimensional segment tree capable of supporting:

- Range Updates
- Range Sum Queries

efficiently across multiple dimensions.

This addresses a well-known limitation of classical multidimensional segment trees.

---

### Modified Lazy Propagation

Traditional lazy propagation cannot be directly extended from one-dimensional to multidimensional segment trees.

The authors introduce two important concepts:

- **Dispersed Updates**
- **Partial Queries**

These techniques enable deferred updates while maintaining correctness across multiple dimensions.

---

### Polylogarithmic Complexity

The proposed data structure achieves efficient performance even as the number of dimensions increases.

Instead of exponential growth in update complexity, operations remain polylogarithmic with respect to the input size.

---

## Complexity Analysis

For a **d-dimensional** segment tree:

| Operation | Complexity |
|------------|------------|
| Range Query | O(log<sup>d</sup> n) |
| Range Update | O(log<sup>d</sup> n) |
| Memory | O(n log<sup>d-1</sup> n) |

where:

- **n** = Number of elements in each dimension.
- **d** = Number of dimensions.

The proposed algorithm maintains polylogarithmic complexity for both range queries and range updates.

---

## Relevance to Our Project

Although this paper studies **multidimensional** rather than **persistent** segment trees, it is highly relevant because it demonstrates that integrating lazy propagation into more complex variants of segment trees is a challenging research problem.

The paper shows that naïvely extending lazy propagation beyond the standard one-dimensional setting is insufficient and requires new algorithmic techniques.

Similarly, our research investigates whether lazy propagation can be integrated correctly into **partially persistent segment trees**, where maintaining multiple historical versions introduces additional challenges.

---

## Research Gap

### What This Paper Addresses

- Multidimensional Segment Trees
- Efficient Range Updates
- Lazy Propagation
- Polylogarithmic Algorithms
- Correctness of Deferred Updates

### What It Does Not Address

- Persistence
- Partial Persistence
- Fully Persistent Segment Trees
- Version-Aware Lazy Propagation
- Historical Range Queries

---

## Connection to Our Research

This paper demonstrates that adding a new capability to segment trees—in this case, multidimensional support—requires redesigning how lazy propagation works.

Our research explores a similar challenge from a different perspective.

Instead of adding more dimensions, we add **multiple historical versions** through partial persistence. The key research question becomes:

> **Can lazy propagation remain correct and efficient when every update creates a new version of the segment tree?**

Thus, both works study how lazy propagation must be adapted when the underlying segment tree becomes more sophisticated.

---

## Key Takeaway

Ibtehaz et al. show that extending lazy propagation to multidimensional segment trees requires new algorithmic ideas and careful correctness analysis. This insight strongly motivates our work, where we investigate a different but equally challenging extension: integrating version-aware lazy propagation into partially persistent segment trees while preserving efficient historical range queries and updates.