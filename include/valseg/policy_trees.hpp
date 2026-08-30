#ifndef VALSEG_POLICY_TREES_HPP
#define VALSEG_POLICY_TREES_HPP

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace valseg {

/**
 * @brief Policy-generic segment trees for the observational-commutativity
 *        theorem (docs/proof.md, section 9).
 *
 * Every class here is a template over a policy from policy.hpp and exists so
 * the theorem's claims can be exercised against code rather than prose:
 *
 *  - RetainedTagPersistentTree is the subject, PersistentLazySegmentTree with
 *    SumAdd replaced by a policy. It retains a copied node's tag through a
 *    partial descent and accumulates ancestor tags outermost-first on a
 *    query, so it is correct exactly when the policy's induced action
 *    transformations commute. It refuses any other policy at compile time.
 *  - CopyOnPushPersistentTree is the ablation from bench/, generalized: the
 *    same layout and invariant, but a partial descent past a tagged node
 *    first pushes the tag into copied children. That preserves chronological
 *    order at every node and admits any valid action monoid.
 *  - PointMaterializedPersistentTree is the tagless path-copying control: an
 *    update descends to every leaf in its range and applies the action there.
 *  - PushedLazyTree is the ordinary in-place lazy tree with push-down; it
 *    keeps only the latest state.
 *
 * The SumAdd instantiations follow the production structures: on the seeded
 * history in tests/policy_trees_test.cpp the arena size after every update and
 * every answer equal PersistentLazySegmentTree's and CopyOnPushSegmentTree's;
 * they differ only where the policy rejects an unrepresentable result, which
 * the SumAdd classes leave to their representable-input precondition. The
 * public SumAdd classes are unchanged; these templates are the research
 * instruments, not a replacement API.
 *
 * Shared operation model and validation contract, in this order:
 *  - a persistent tree with no versions rejects updates and queries with
 *    std::runtime_error;
 *  - an invalid version is std::out_of_range;
 *  - an empty array is std::runtime_error, left > right is
 *    std::invalid_argument, right >= size() is std::out_of_range;
 *  - a policy operation may throw (std::overflow_error for the integer
 *    policies); the update then publishes nothing and appends nothing.
 *
 * An update whose action equals Policy::actionIdentity() publishes a version
 * that shares the latest root and allocates no node, the generic form of the
 * zero-delta fast path. Queries allocate nothing and mutate nothing.
 */

namespace detail {

/**
 * @brief Reject an operation on a tree that has published no version.
 *
 * @param versionCount Number of published versions.
 *
 * @throws std::runtime_error if no version exists.
 */
inline void requireVersions(std::size_t versionCount) {
  if (versionCount == 0) {
    throw std::runtime_error("Tree has no versions.");
  }
}

/**
 * @brief Validate a version identifier.
 *
 * @param versionCount Number of published versions.
 * @param version      Version to read.
 *
 * @throws std::out_of_range if version is not published.
 */
inline void requireVersion(std::size_t versionCount, std::size_t version) {
  if (version >= versionCount) {
    throw std::out_of_range("Invalid version number.");
  }
}

/**
 * @brief Validate an inclusive index range in the shared contract order.
 *
 * @param arraySize Number of elements.
 * @param left      Left index (inclusive).
 * @param right     Right index (inclusive).
 *
 * @throws std::runtime_error     The array is empty.
 * @throws std::invalid_argument  left is greater than right.
 * @throws std::out_of_range      right is not smaller than arraySize.
 */
inline void requireRange(std::size_t arraySize, std::size_t left, std::size_t right) {
  if (arraySize == 0) {
    throw std::runtime_error("Tree is empty.");
  }
  if (left > right) {
    throw std::invalid_argument("Left index is greater than right index.");
  }
  if (right >= arraySize) {
    throw std::out_of_range("Range exceeds array size.");
  }
}

/**
 * @brief Truncate an append-only arena back to a checkpoint after a failed
 *        update, without requiring a default-constructible record type.
 *
 * @param arena      Arena to truncate.
 * @param checkpoint Size to restore.
 */
template <class Record> void rollBack(std::vector<Record>& arena, std::size_t checkpoint) {
  arena.erase(std::next(arena.begin(), static_cast<std::ptrdiff_t>(checkpoint)), arena.end());
}

} // namespace detail

/**
 * @brief Path-copying persistent segment tree with lazy tags, parameterized
 *        by the one decision that separates the subject from its ablation.
 *
 * With kPushOnPartialDescent false this is the tag-retaining subject; with it
 * true this is copy-on-push. Use the RetainedTagPersistentTree and
 * CopyOnPushPersistentTree aliases rather than this name.
 *
 * Node invariant, for a node covering a segment in a published version:
 *  - aggregate is the exact aggregate of the segment, excluding tags held by
 *    the node's proper ancestors;
 *  - the node's own tag is already applied inside aggregate;
 *  - the tag is not applied inside either child's aggregate, so
 *    aggregate = apply(tag, combine(left.aggregate, right.aggregate), len).
 *
 * @tparam Policy                 Aggregate/action policy from policy.hpp.
 * @tparam kPushOnPartialDescent  Whether a partial descent pushes the node's
 *                                tag into copied children before continuing.
 */
template <class Policy, bool kPushOnPartialDescent> class PathCopyingActionTree {
  static_assert(kPushOnPartialDescent || Policy::kInducedActionsCommute,
                "RetainedTagPersistentTree needs Policy::kInducedActionsCommute: a tag retained "
                "on an ancestor is applied outside newer tags below it. Use "
                "CopyOnPushPersistentTree for a policy whose induced actions do not commute.");

public:
  /** @brief Aggregate type of the policy. */
  using Aggregate = typename Policy::Aggregate;

  /** @brief Action type of the policy. */
  using Action = typename Policy::Action;

  /**
   * @brief Construct a tree with no versions.
   */
  PathCopyingActionTree() = default;

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial per-element aggregates; an empty array publishes an
   *               empty version 0.
   */
  explicit PathCopyingActionTree(const std::vector<Aggregate>& values) {
    initialize(values);
  }

  /**
   * @brief Build version 0, replacing all previous versions and nodes.
   *
   * Replacement state is built first and swapped in only after the build
   * succeeds, so a failed initialization leaves the tree unchanged.
   *
   * @param values Initial per-element aggregates; an empty array publishes an
   *               empty version 0.
   */
  void initialize(const std::vector<Aggregate>& values) {
    std::vector<Node> newNodes;
    std::vector<std::size_t> newRoots;
    if (values.empty()) {
      newRoots.push_back(noNode);
    } else {
      newNodes.reserve(2 * values.size() - 1);
      newRoots.push_back(build(values, newNodes, 0, values.size() - 1));
    }
    nodes.swap(newNodes);
    roots.swap(newRoots);
    arraySize = values.size();
  }

  /**
   * @brief Apply an action to every element in [left, right] of the latest
   *        version and publish the result as a new version.
   *
   * @param left   Left index (inclusive).
   * @param right  Right index (inclusive).
   * @param action Action to apply.
   *
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   *
   * Any exception thrown by a policy operation propagates after the arena is
   * rolled back; no version is published.
   */
  std::size_t rangeApply(std::size_t left, std::size_t right, const Action& action) {
    detail::requireVersions(roots.size());
    detail::requireRange(arraySize, left, right);
    if (action == Policy::actionIdentity()) {
      roots.push_back(roots.back());
      return roots.size() - 1;
    }
    const std::size_t checkpoint = nodes.size();
    try {
      roots.push_back(update(roots.back(), 0, arraySize - 1, left, right, action));
    } catch (...) {
      detail::rollBack(nodes, checkpoint);
      throw;
    }
    return roots.size() - 1;
  }

  /**
   * @brief Aggregate over [left, right] in a published version.
   *
   * @param version Published version to read.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   *
   * @return The aggregate, combined in index order.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::out_of_range      Invalid version number.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  Aggregate rangeAggregate(std::size_t version, std::size_t left, std::size_t right) const {
    detail::requireVersions(roots.size());
    detail::requireVersion(roots.size(), version);
    detail::requireRange(arraySize, left, right);
    return query(roots[version], 0, arraySize - 1, left, right, Policy::actionIdentity());
  }

  /**
   * @brief Number of published versions.
   *
   * @return Number of published versions, or zero before initialization.
   */
  std::size_t versionCount() const {
    return roots.size();
  }

  /**
   * @brief Number of elements in every version.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const {
    return arraySize;
  }

  /**
   * @brief Total number of nodes retained in the arena.
   *
   * @return Arena size across every published version.
   */
  std::size_t nodeCount() const {
    return nodes.size();
  }

private:
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    Aggregate aggregate;
    Action tag;
  };

  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  std::vector<Node> nodes;
  std::vector<std::size_t> roots;
  std::size_t arraySize = 0;

  static std::size_t build(const std::vector<Aggregate>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight) {
    if (segmentLeft == segmentRight) {
      arena.push_back(Node{noNode, noNode, values[segmentLeft], Policy::actionIdentity()});
      return arena.size() - 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    const std::size_t leftRoot = build(values, arena, segmentLeft, middle);
    const std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);
    arena.push_back(Node{leftRoot, rightRoot,
                         Policy::combine(arena[leftRoot].aggregate, arena[rightRoot].aggregate),
                         Policy::actionIdentity()});
    return arena.size() - 1;
  }

  // Copy `source` with `tag` applied over its `length` elements and composed
  // into its own tag. This is the push: the copy-on-push variant allocates
  // it, the tag-retaining variant never calls it.
  std::size_t pushInto(std::size_t source, const Action& tag, std::size_t length) {
    const Node child = nodes[source];
    nodes.push_back(Node{child.leftChild, child.rightChild,
                         Policy::apply(tag, child.aggregate, length),
                         Policy::compose(tag, child.tag)});
    return nodes.size() - 1;
  }

  std::size_t update(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t queryLeft, std::size_t queryRight, const Action& action) {
    // Copy the node by value: appending below may reallocate the arena.
    const Node current = nodes[nodeIndex];
    const std::size_t length = segmentRight - segmentLeft + 1;

    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      // Full coverage: the action is newest, so it composes outside the
      // node's existing tag; both children stay shared.
      nodes.push_back(Node{current.leftChild, current.rightChild,
                           Policy::apply(action, current.aggregate, length),
                           Policy::compose(action, current.tag)});
      return nodes.size() - 1;
    }

    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    std::size_t leftChild = current.leftChild;
    std::size_t rightChild = current.rightChild;
    Action retained = current.tag;

    if constexpr (kPushOnPartialDescent) {
      if (!(current.tag == Policy::actionIdentity())) {
        leftChild = pushInto(leftChild, current.tag, middle - segmentLeft + 1);
        rightChild = pushInto(rightChild, current.tag, segmentRight - middle);
        retained = Policy::actionIdentity();
      }
    }

    std::size_t newLeft = leftChild;
    std::size_t newRight = rightChild;
    if (queryLeft <= middle) {
      newLeft = update(leftChild, segmentLeft, middle, queryLeft, queryRight, action);
    }
    if (queryRight > middle) {
      newRight = update(rightChild, middle + 1, segmentRight, queryLeft, queryRight, action);
    }

    // The copied parent keeps `retained` (its own tag, or the identity after
    // a push) applied outside the children's aggregates.
    const Aggregate children = Policy::combine(nodes[newLeft].aggregate, nodes[newRight].aggregate);
    nodes.push_back(Node{newLeft, newRight, Policy::apply(retained, children, length), retained});
    return nodes.size() - 1;
  }

  Aggregate query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight, const Action& inherited) const {
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return Policy::aggregateIdentity();
    }
    const Node& current = nodes[nodeIndex];
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      return Policy::apply(inherited, current.aggregate, segmentRight - segmentLeft + 1);
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    // Outermost-first accumulation: the inherited composition is outer, this
    // node's tag inner, so compose(inherited, tag) applies the tag first.
    const Action next = Policy::compose(inherited, current.tag);
    return Policy::combine(
        query(current.leftChild, segmentLeft, middle, queryLeft, queryRight, next),
        query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, next));
  }
};

/**
 * @brief The tag-retaining subject: PersistentLazySegmentTree over a policy.
 *
 * Instantiating it with a policy whose kInducedActionsCommute is false is a
 * compile-time error; tests/compile_fail/ checks that rejection.
 *
 * @tparam Policy Aggregate/action policy with commuting induced actions.
 */
template <class Policy> using RetainedTagPersistentTree = PathCopyingActionTree<Policy, false>;

/**
 * @brief The copy-on-push ablation over a policy: any valid action monoid.
 *
 * @tparam Policy Aggregate/action policy.
 */
template <class Policy> using CopyOnPushPersistentTree = PathCopyingActionTree<Policy, true>;

/**
 * @brief Tagless path-copying persistent tree: every update is materialized
 *        at the leaves it covers, in chronological order.
 *
 * Admits any valid action monoid. Same operation model, validation order and
 * identity-action fast path as PathCopyingActionTree; update cost grows with
 * the range width.
 *
 * @tparam Policy Aggregate/action policy from policy.hpp.
 */
template <class Policy> class PointMaterializedPersistentTree {
public:
  /** @brief Aggregate type of the policy. */
  using Aggregate = typename Policy::Aggregate;

  /** @brief Action type of the policy. */
  using Action = typename Policy::Action;

  /**
   * @brief Construct a tree with no versions.
   */
  PointMaterializedPersistentTree() = default;

  /**
   * @brief Construct version 0 from an initial array.
   *
   * @param values Initial per-element aggregates; may be empty.
   */
  explicit PointMaterializedPersistentTree(const std::vector<Aggregate>& values) {
    initialize(values);
  }

  /**
   * @brief Build version 0, replacing all previous versions and nodes.
   *
   * @param values Initial per-element aggregates; may be empty.
   */
  void initialize(const std::vector<Aggregate>& values) {
    std::vector<Node> newNodes;
    std::vector<std::size_t> newRoots;
    if (values.empty()) {
      newRoots.push_back(noNode);
    } else {
      newNodes.reserve(2 * values.size() - 1);
      newRoots.push_back(build(values, newNodes, 0, values.size() - 1));
    }
    nodes.swap(newNodes);
    roots.swap(newRoots);
    arraySize = values.size();
  }

  /**
   * @brief Apply an action to every element in [left, right] of the latest
   *        version and publish the result as a new version.
   *
   * @param left   Left index (inclusive).
   * @param right  Right index (inclusive).
   * @param action Action to apply.
   *
   * @return Version number of the newly published version.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  std::size_t rangeApply(std::size_t left, std::size_t right, const Action& action) {
    detail::requireVersions(roots.size());
    detail::requireRange(arraySize, left, right);
    if (action == Policy::actionIdentity()) {
      roots.push_back(roots.back());
      return roots.size() - 1;
    }
    const std::size_t checkpoint = nodes.size();
    try {
      roots.push_back(update(roots.back(), 0, arraySize - 1, left, right, action));
    } catch (...) {
      detail::rollBack(nodes, checkpoint);
      throw;
    }
    return roots.size() - 1;
  }

  /**
   * @brief Aggregate over [left, right] in a published version.
   *
   * @param version Published version to read.
   * @param left    Left index (inclusive).
   * @param right   Right index (inclusive).
   *
   * @return The aggregate, combined in index order.
   *
   * @throws std::runtime_error     No initialized version or empty structure.
   * @throws std::out_of_range      Invalid version number.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  Aggregate rangeAggregate(std::size_t version, std::size_t left, std::size_t right) const {
    detail::requireVersions(roots.size());
    detail::requireVersion(roots.size(), version);
    detail::requireRange(arraySize, left, right);
    return query(roots[version], 0, arraySize - 1, left, right);
  }

  /**
   * @brief Number of published versions.
   *
   * @return Number of published versions, or zero before initialization.
   */
  std::size_t versionCount() const {
    return roots.size();
  }

  /**
   * @brief Number of elements in every version.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const {
    return arraySize;
  }

  /**
   * @brief Total number of nodes retained in the arena.
   *
   * @return Arena size across every published version.
   */
  std::size_t nodeCount() const {
    return nodes.size();
  }

private:
  struct Node {
    std::size_t leftChild;
    std::size_t rightChild;
    Aggregate aggregate;
  };

  static constexpr std::size_t noNode = static_cast<std::size_t>(-1);

  std::vector<Node> nodes;
  std::vector<std::size_t> roots;
  std::size_t arraySize = 0;

  static std::size_t build(const std::vector<Aggregate>& values, std::vector<Node>& arena,
                           std::size_t segmentLeft, std::size_t segmentRight) {
    if (segmentLeft == segmentRight) {
      arena.push_back(Node{noNode, noNode, values[segmentLeft]});
      return arena.size() - 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    const std::size_t leftRoot = build(values, arena, segmentLeft, middle);
    const std::size_t rightRoot = build(values, arena, middle + 1, segmentRight);
    arena.push_back(Node{leftRoot, rightRoot,
                         Policy::combine(arena[leftRoot].aggregate, arena[rightRoot].aggregate)});
    return arena.size() - 1;
  }

  std::size_t update(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                     std::size_t queryLeft, std::size_t queryRight, const Action& action) {
    const Node current = nodes[nodeIndex];
    if (segmentLeft == segmentRight) {
      // Only intersecting segments are entered, so this leaf is in range.
      nodes.push_back(Node{noNode, noNode, Policy::apply(action, current.aggregate, 1)});
      return nodes.size() - 1;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    std::size_t newLeft = current.leftChild;
    std::size_t newRight = current.rightChild;
    if (queryLeft <= middle) {
      newLeft = update(current.leftChild, segmentLeft, middle, queryLeft, queryRight, action);
    }
    if (queryRight > middle) {
      newRight =
          update(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight, action);
    }
    nodes.push_back(Node{newLeft, newRight,
                         Policy::combine(nodes[newLeft].aggregate, nodes[newRight].aggregate)});
    return nodes.size() - 1;
  }

  Aggregate query(std::size_t nodeIndex, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight) const {
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return Policy::aggregateIdentity();
    }
    const Node& current = nodes[nodeIndex];
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      return current.aggregate;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    return Policy::combine(
        query(current.leftChild, segmentLeft, middle, queryLeft, queryRight),
        query(current.rightChild, middle + 1, segmentRight, queryLeft, queryRight));
  }
};

/**
 * @brief Ordinary lazy segment tree with in-place push-down over a policy.
 *
 * Not persistent: updates mutate the current state and queries push pending
 * tags into the nodes they visit. Push-down before every descent keeps the
 * tags on any root-to-leaf path in chronological order, so any valid action
 * monoid is admitted. LazySegmentTree is this class fixed to SumAdd. There is
 * no rollback: a policy operation that throws mid-push leaves the tree in an
 * unspecified state, so it is unusable after a policy exception.
 *
 * @tparam Policy Aggregate/action policy from policy.hpp.
 */
template <class Policy> class PushedLazyTree {
public:
  /** @brief Aggregate type of the policy. */
  using Aggregate = typename Policy::Aggregate;

  /** @brief Action type of the policy. */
  using Action = typename Policy::Action;

  /**
   * @brief Construct an empty tree.
   */
  PushedLazyTree() = default;

  /**
   * @brief Construct a tree over an initial array.
   *
   * @param values Initial per-element aggregates; an empty array leaves the
   *               tree empty.
   */
  explicit PushedLazyTree(const std::vector<Aggregate>& values) {
    initialize(values);
  }

  /**
   * @brief Build the tree, discarding all previous state.
   *
   * @param values Initial per-element aggregates; an empty array leaves the
   *               tree empty.
   */
  void initialize(const std::vector<Aggregate>& values) {
    arraySize = values.size();
    aggregates.assign(4 * arraySize, Policy::aggregateIdentity());
    tags.assign(4 * arraySize, Policy::actionIdentity());
    if (arraySize > 0) {
      build(values, 0, 0, arraySize - 1);
    }
  }

  /**
   * @brief Apply an action to every element in [left, right] in place.
   *
   * @param left   Left index (inclusive).
   * @param right  Right index (inclusive).
   * @param action Action to apply.
   *
   * @throws std::runtime_error     The tree is empty.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  void rangeApply(std::size_t left, std::size_t right, const Action& action) {
    detail::requireRange(arraySize, left, right);
    update(0, 0, arraySize - 1, left, right, action);
  }

  /**
   * @brief Aggregate over [left, right] in the current state.
   *
   * Not const: pending tags are pushed into the visited nodes.
   *
   * @param left  Left index (inclusive).
   * @param right Right index (inclusive).
   *
   * @return The aggregate, combined in index order.
   *
   * @throws std::runtime_error     The tree is empty.
   * @throws std::invalid_argument  left is greater than right.
   * @throws std::out_of_range      right is not smaller than size().
   */
  Aggregate rangeAggregate(std::size_t left, std::size_t right) {
    detail::requireRange(arraySize, left, right);
    return query(0, 0, arraySize - 1, left, right);
  }

  /**
   * @brief Number of elements.
   *
   * @return Number of elements, or zero before initialization.
   */
  std::size_t size() const {
    return arraySize;
  }

private:
  std::vector<Aggregate> aggregates;
  std::vector<Action> tags;
  std::size_t arraySize = 0;

  void build(const std::vector<Aggregate>& values, std::size_t node, std::size_t segmentLeft,
             std::size_t segmentRight) {
    if (segmentLeft == segmentRight) {
      aggregates[node] = values[segmentLeft];
      return;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    build(values, 2 * node + 1, segmentLeft, middle);
    build(values, 2 * node + 2, middle + 1, segmentRight);
    aggregates[node] = Policy::combine(aggregates[2 * node + 1], aggregates[2 * node + 2]);
  }

  // Apply the pending tag to this node and hand it to the children, newest
  // outside: the parent's tag is newer than anything already pending below.
  void push(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight) {
    if (tags[node] == Policy::actionIdentity()) {
      return;
    }
    aggregates[node] = Policy::apply(tags[node], aggregates[node], segmentRight - segmentLeft + 1);
    if (segmentLeft != segmentRight) {
      tags[2 * node + 1] = Policy::compose(tags[node], tags[2 * node + 1]);
      tags[2 * node + 2] = Policy::compose(tags[node], tags[2 * node + 2]);
    }
    tags[node] = Policy::actionIdentity();
  }

  void update(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
              std::size_t queryLeft, std::size_t queryRight, const Action& action) {
    push(node, segmentLeft, segmentRight);
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return;
    }
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      tags[node] = Policy::compose(action, tags[node]);
      push(node, segmentLeft, segmentRight);
      return;
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    update(2 * node + 1, segmentLeft, middle, queryLeft, queryRight, action);
    update(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight, action);
    aggregates[node] = Policy::combine(aggregates[2 * node + 1], aggregates[2 * node + 2]);
  }

  Aggregate query(std::size_t node, std::size_t segmentLeft, std::size_t segmentRight,
                  std::size_t queryLeft, std::size_t queryRight) {
    push(node, segmentLeft, segmentRight);
    if (segmentRight < queryLeft || segmentLeft > queryRight) {
      return Policy::aggregateIdentity();
    }
    if (queryLeft <= segmentLeft && segmentRight <= queryRight) {
      return aggregates[node];
    }
    const std::size_t middle = (segmentLeft + segmentRight) / 2;
    return Policy::combine(query(2 * node + 1, segmentLeft, middle, queryLeft, queryRight),
                           query(2 * node + 2, middle + 1, segmentRight, queryLeft, queryRight));
  }
};

} // namespace valseg

#endif
