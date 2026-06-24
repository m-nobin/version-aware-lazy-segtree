# Literature Review: Making Data Structures Confluently Persistent (2003)

## Paper Information

**Title:** Making Data Structures Confluently Persistent

**Authors:**
- Erik D. Demaine
- John Iacono
- Stefan Langerman

**Paper Link:**
https://www.researchgate.net/publication/220406882_Making_data_structures_confluently_persistent

---

## Summary

Demaine et al. (2003) extended the concept of persistence beyond partial and full persistence by introducing **confluent persistence**. The paper studies how multiple versions of a data structure can be combined (merged) into a new version while preserving access to historical versions. Unlike traditional persistence models, confluent persistence allows both branching and merging of versions, creating a version graph rather than a simple version tree.

---

## Confluent Persistence

- Query any version.
- Update any version.
- Merge multiple versions into a new version.

Example:

```text
V1 ----\
         \
          V4
         /
V2 ----/
```

Version V4 is created by combining V1 and V2.

---

## Key Contributions

### Confluent Persistence

The paper formally introduced and analyzed confluent persistence, which generalizes both partial and full persistence.

### Version DAG

Instead of maintaining versions as a tree, the paper represents versions as a Directed Acyclic Graph (DAG), allowing multiple parent versions.

---

## Relevance to Our Project

This paper provides a broader understanding of persistence and demonstrates how persistence research evolved beyond the original models proposed by Driscoll et al. (1989).

Although our project focuses on **partial persistence**, this paper highlights the increasing complexity of version management when branching and merging are introduced.

---

## Relation to Our Research

### What This Paper Addresses

- Partial persistence
- Full persistence
- Confluent persistence
- Version management
- Efficient persistence techniques

### What It Does Not Address

- Segment trees
- Lazy propagation
- Range updates
- Version-aware lazy tags

---

## Key Takeaway

Confluent persistence extends traditional persistence by allowing versions to be merged in addition to queried and updated. While our project does not require version merging, this paper provides important background on advanced persistence models and highlights why partial persistence remains a practical choice for version-aware lazy propagation in segment trees.