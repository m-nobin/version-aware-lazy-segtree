# Version-Aware Lazy Propagation for Partially Persistent Segment Trees

This repository contains an Advanced Algorithms semester project on combining lazy propagation with partial persistence in segment trees.

## Problem

Support range-add updates and range-sum queries over multiple historical versions of an array.

```text
build(A) -> version 0
range_update(l, r, delta) -> creates a new version
range_query(version_id, l, r) -> sum in selected version
