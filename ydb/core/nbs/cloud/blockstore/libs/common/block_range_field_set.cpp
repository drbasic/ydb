#include "block_range_field_set.h"

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/builder.h>

#include <new>

namespace NYdb::NBS::NBlockStore {

namespace {

constexpr size_t ArenaSize = 512;
constexpr size_t MaxNodeCount = ArenaSize / TNodeHelper::NodeSize;

struct TNode
{
    TBlockRange<ui16> Range;
    ui16 Left = 0xFFFF;
    ui16 Right = 0xFFFF;
};

TNode* NodeAt(TBlockRangeFieldSet& self, ui16 idx)
{
    return static_cast<TNode*>(TNodeHelper::NodeAt(self, idx));
}

const TNode* NodeAt(const TBlockRangeFieldSet& self, ui16 idx)
{
    return static_cast<const TNode*>(TNodeHelper::NodeAt(self, idx));
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

void TBlockRangeFieldSet::EnsureArena()
{
    if (ArenaAllocated_) {
        return;
    }
    Arena_ = static_cast<char*>(::operator new(ArenaSize));
    ArenaAllocated_ = true;
}

ui16 TBlockRangeFieldSet::AllocNode()
{
    if (FreeHead_ != NodeNullIndex) {
        ui16 idx = FreeHead_;
        FreeHead_ = NodeAt(*this, idx)->Left;
        return idx;
    }
    if (UsedCount_ >= MaxNodeCount) {
        return NodeNullIndex;
    }
    const ui16 idx = UsedCount_++;
    // Zero out Left/Right — they contain garbage from raw arena memory.
    NodeAt(*this, idx)->Left = NodeNullIndex;
    NodeAt(*this, idx)->Right = NodeNullIndex;
    return idx;
}

void TBlockRangeFieldSet::FreeNode(ui16 idx)
{
    if (idx == NodeNullIndex) {
        return;
    }
    NodeAt(*this, idx)->Left = FreeHead_;
    FreeHead_ = idx;
}

void TBlockRangeFieldSet::InsertNode(ui16 idx)
{
    if (Root_ == NodeNullIndex) {
        Root_ = idx;
        return;
    }
    const ui16 key = NodeAt(*this, idx)->Range.Start;
    ui16 current = Root_;
    while (true) {
        TNode* curNode = NodeAt(*this, current);
        if (key < curNode->Range.Start) {
            if (curNode->Left == NodeNullIndex) {
                curNode->Left = idx;
                return;
            }
            current = curNode->Left;
        } else {
            if (curNode->Right == NodeNullIndex) {
                curNode->Right = idx;
                return;
            }
            current = curNode->Right;
        }
    }
}

ui16 TBlockRangeFieldSet::FindLowerBoundNode(ui16 key) const
{
    ui16 current = Root_;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = NodeAt(*this, current);
        if (node->Range.Start >= key) {
            best = current;
            current = node->Left;
        } else {
            current = node->Right;
        }
    }
    return best;
}

ui16 TBlockRangeFieldSet::FindPredecessorNode(ui16 key) const
{
    ui16 current = Root_;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = NodeAt(*this, current);
        if (node->Range.Start < key) {
            best = current;
            current = node->Right;
        } else {
            current = node->Left;
        }
    }
    return best;
}

ui16 TBlockRangeFieldSet::FindNextGreaterNode(ui16 key) const
{
    ui16 current = Root_;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = NodeAt(*this, current);
        if (node->Range.Start > key) {
            best = current;
            current = node->Left;
        } else {
            current = node->Right;
        }
    }
    return best;
}

ui16 TBlockRangeFieldSet::FindNodeByKey(
    ui16 key,
    ui16& parent,
    bool& isLeftChild) const
{
    ui16 current = Root_;
    parent = NodeNullIndex;
    isLeftChild = false;
    while (current != NodeNullIndex) {
        const TNode* node = NodeAt(*this, current);
        if (node->Range.Start == key) {
            return current;
        }
        parent = current;
        if (key < node->Range.Start) {
            isLeftChild = true;
            current = node->Left;
        } else {
            isLeftChild = false;
            current = node->Right;
        }
    }
    return NodeNullIndex;
}

void TBlockRangeFieldSet::RemoveNodeByKey(ui16 key)
{
    ui16 parent = NodeNullIndex;
    bool isLeftChild = false;
    const ui16 idx = FindNodeByKey(key, parent, isLeftChild);
    if (idx == NodeNullIndex) {
        return;
    }
    TNode* node = NodeAt(*this, idx);

    // Attach a replacement child to the parent (or make it the root).
    const auto attach = [&](ui16 replacement)
    {
        if (parent == NodeNullIndex) {
            Root_ = replacement;
        } else if (isLeftChild) {
            NodeAt(*this, parent)->Left = replacement;
        } else {
            NodeAt(*this, parent)->Right = replacement;
        }
    };

    if (node->Left == NodeNullIndex) {
        // Leaf or right child only.
        attach(node->Right);
        FreeNode(idx);
        return;
    }
    if (node->Right == NodeNullIndex) {
        // Left child only.
        attach(node->Left);
        FreeNode(idx);
        return;
    }

    // Two children: replace the range with the in-order successor's one,
    // then unlink the successor (it has no left child by definition).
    ui16 successor = node->Right;
    ui16 successorParent = idx;
    while (NodeAt(*this, successor)->Left != NodeNullIndex) {
        successorParent = successor;
        successor = NodeAt(*this, successor)->Left;
    }
    TNode* succNode = NodeAt(*this, successor);
    node->Range = succNode->Range;
    if (successorParent == idx) {
        node->Right = succNode->Right;
    } else {
        NodeAt(*this, successorParent)->Left = succNode->Right;
    }
    FreeNode(successor);
}

////////////////////////////////////////////////////////////////////////////////

TBlockRangeFieldSet::TBlockRangeFieldSet() noexcept = default;

TBlockRangeFieldSet::~TBlockRangeFieldSet()
{
    if (Arena_) {
        ::operator delete(Arena_);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldSet::Add(TRange range)
{
    if (OutOfMemory_) {
        return false;
    }

    // Add algorithm:
    // 1. Find the first node overlapping or adjacent to range.
    //    If no overlap, plain insertion.
    // 2. Fast path: if the first node fully covers range — return false.
    // 3. Expand the first node in place:
    //      first.Start = min(first.Start, range.Start)
    //      first.End   = max(first.End,   range.End)
    // 4. Walk right through successors:
    //    - nodes fully covered by the expanded first (S.End <= first.End)
    //      → remove by Start key;
    //    - first node S with a right edge wider than ours
    //      (S.Start <= first.End + 1, but S.End > first.End)
    //      → first.End = S.End, remove S, exit (the rest is guaranteed
    //        to have a gap ≥ 1 block by the non-overlap invariant).

    if (Root_ == NodeNullIndex) {
        // Empty tree: plain insertion.
        EnsureArena();
        const ui16 idx = AllocNode();
        if (idx == NodeNullIndex) {
            OutOfMemory_ = true;
            return false;
        }
        NodeAt(*this, idx)->Range = range;
        InsertNode(idx);
        return true;
    }

    // Find the first node overlapping or adjacent to the range:
    // either the predecessor touching us from the left,
    // or the lower bound node touching us from the right.
    const ui16 pred = FindPredecessorNode(range.Start);
    const ui16 lower = FindLowerBoundNode(range.Start);

    ui16 first = NodeNullIndex;
    if (pred != NodeNullIndex &&
        NodeAt(*this, pred)->Range.End + 1 >= range.Start)
    {
        first = pred;
    } else if (
        lower != NodeNullIndex &&
        NodeAt(*this, lower)->Range.Start <= range.End + 1)
    {
        first = lower;
    }

    if (first == NodeNullIndex) {
        // No overlap and no adjacency: plain insertion.
        EnsureArena();
        const ui16 idx = AllocNode();
        if (idx == NodeNullIndex) {
            OutOfMemory_ = true;
            return false;
        }
        NodeAt(*this, idx)->Range = range;
        InsertNode(idx);
        return true;
    }

    TNode* firstNode = NodeAt(*this, first);

    // Fast path: fully covered by an existing range — nothing changes.
    if (firstNode->Range.Start <= range.Start &&
        range.End <= firstNode->Range.End)
    {
        return false;
    }

    // Expand the first node in place (its Start may decrease, but it stays
    // greater than the predecessor's End, so the BST order is preserved).
    if (range.Start < firstNode->Range.Start) {
        firstNode->Range.Start = range.Start;
    }
    if (range.End > firstNode->Range.End) {
        firstNode->Range.End = range.End;
    }

    // Absorb following nodes covered by the expanded range.
    while (true) {
        const ui16 next = FindNextGreaterNode(firstNode->Range.Start);
        if (next == NodeNullIndex) {
            break;
        }
        TNode* nextNode = NodeAt(*this, next);
        if (nextNode->Range.Start > firstNode->Range.End + 1) {
            // Gap: the expanded range ends before this node.
            break;
        }
        if (nextNode->Range.End > firstNode->Range.End) {
            // Partial overlap: widen our right edge, remove the node,
            // and stop (the rest of the tree is guaranteed to be
            // non-adjacent due to the invariant).
            firstNode->Range.End = nextNode->Range.End;
            const ui16 nextStart = nextNode->Range.Start;
            RemoveNodeByKey(nextStart);
            break;
        }
        // Fully covered: remove and continue scanning.
        const ui16 nextStart = nextNode->Range.Start;
        RemoveNodeByKey(nextStart);
    }

    return true;
}

bool TBlockRangeFieldSet::Add(const TBlockRangeFieldSet& field)
{
    if (this == &field) {
        return false;
    }

    bool changed = false;
    field.Enumerate(
        [&](TRange r)
        {
            changed |= Add(r);
            return EEnumerateContinuation::Continue;
        });
    return changed;
}

bool TBlockRangeFieldSet::Remove(TRange range)
{
    if (Root_ == NodeNullIndex) {
        return false;
    }

    // Remove algorithm:
    // 1. Fast path: O(h) walk — if nothing overlaps, return false.
    // 2. Find the first node overlapping range.
    // 3. Walk the continuous run of overlapping nodes left to right:
    //    - range fully inside node → trim left (End = range.Start - 1),
    //      insert a new node on the right (range.End+1 .. node.End);
    //    - left overlap (node.Start < range.Start <= node.End)
    //      → trim right (node.End = range.Start - 1), continue;
    //    - right overlap (node.End > range.End >= node.Start)
    //      → trim left (node.Start = range.End + 1), exit;
    //    - node fully covered → remove by Start key, continue.

    // Fast path: check that something actually overlaps the range.
    {
        ui16 current = Root_;
        bool found = false;
        while (current != NodeNullIndex) {
            const TNode* node = NodeAt(*this, current);
            if (node->Range.End < range.Start) {
                current = node->Right;
            } else if (node->Range.Start > range.End) {
                current = node->Left;
            } else {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    // Find the first node overlapping the range (adjacency does not count).
    const ui16 pred = FindPredecessorNode(range.Start);
    const ui16 lower = FindLowerBoundNode(range.Start);

    ui16 current = NodeNullIndex;
    if (pred != NodeNullIndex && NodeAt(*this, pred)->Range.End >= range.Start)
    {
        current = pred;
    } else if (
        lower != NodeNullIndex &&
        NodeAt(*this, lower)->Range.Start <= range.End)
    {
        current = lower;
    } else {
        return false;   // Unreachable after the fast path check.
    }

    // Process the run of overlapping nodes from left to right.
    while (current != NodeNullIndex &&
           NodeAt(*this, current)->Range.Start <= range.End)
    {
        const ui16 nodeStart = NodeAt(*this, current)->Range.Start;
        const ui16 nodeEnd = NodeAt(*this, current)->Range.End;

        if (nodeStart < range.Start && nodeEnd > range.End) {
            // The range cuts a hole inside this node: trim the left part
            // and insert the right part as a new node.
            NodeAt(*this, current)->Range.End = range.Start - 1;
            const ui16 idx = AllocNode();
            if (idx == NodeNullIndex) {
                OutOfMemory_ = true;
                return false;
            }
            NodeAt(*this, idx)->Range =
                TRange::MakeClosedInterval(range.End + 1, nodeEnd);
            InsertNode(idx);
            break;
        }
        if (nodeStart < range.Start) {
            // Overlap on the left: trim the right edge in place.
            // The key (Start) does not change, the order is preserved.
            NodeAt(*this, current)->Range.End = range.Start - 1;
            current = FindNextGreaterNode(nodeStart);
            continue;
        }
        if (nodeEnd > range.End) {
            // Overlap on the right: trim the left edge in place.
            // The new Start is still greater than the previous node's End
            // and less than the next node's Start, so the order holds.
            NodeAt(*this, current)->Range.Start = range.End + 1;
            break;
        }
        // Fully covered: remove and continue with the next node.
        current = FindNextGreaterNode(nodeStart);
        RemoveNodeByKey(nodeStart);
    }

    return true;
}

bool TBlockRangeFieldSet::Remove(const TBlockRangeFieldSet& field)
{
    if (this == &field) {
        return Clear();
    }

    bool changed = false;
    field.Enumerate(
        [&](TRange r)
        {
            changed |= Remove(r);
            return EEnumerateContinuation::Continue;
        });
    return changed;
}

bool TBlockRangeFieldSet::Clear()
{
    const bool changed = Root_ != NodeNullIndex;
    if (Arena_) {
        ::operator delete(Arena_);
        Arena_ = nullptr;
    }
    Root_ = NodeNullIndex;
    FreeHead_ = NodeNullIndex;
    UsedCount_ = 0;
    OutOfMemory_ = false;
    ArenaAllocated_ = false;
    return changed;
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldSet::Overlaps(TRange other) const
{
    if (Root_ == NodeNullIndex) {
        return false;
    }

    ui16 current = Root_;
    while (current != NodeNullIndex) {
        const TNode* node = NodeAt(*this, current);
        if (node->Range.End < other.Start) {
            current = node->Right;
        } else if (node->Range.Start > other.End) {
            current = node->Left;
        } else {
            return true;
        }
    }

    return false;
}

bool TBlockRangeFieldSet::Overlaps(const TBlockRangeFieldSet& other) const
{
    // Both lists are sorted by Start and internally disjoint,
    // so a linear merge scan is sufficient.
    TVector<TRange> leftRanges, rightRanges;
    Enumerate(
        [&](TRange r)
        {
            leftRanges.push_back(r);
            return EEnumerateContinuation::Continue;
        });
    other.Enumerate(
        [&](TRange r)
        {
            rightRanges.push_back(r);
            return EEnumerateContinuation::Continue;
        });

    size_t li = 0, ri = 0;
    while (li < leftRanges.size() && ri < rightRanges.size()) {
        if (leftRanges[li].End < rightRanges[ri].Start) {
            ++li;
        } else if (rightRanges[ri].End < leftRanges[li].Start) {
            ++ri;
        } else {
            return true;
        }
    }

    return false;
}

void TBlockRangeFieldSet::Enumerate(TEnumerateFunc func) const
{
    if (Root_ == NodeNullIndex) {
        return;
    }

    ui16 current = Root_;
    TVector<ui16> stack;

    while (current != NodeNullIndex || !stack.empty()) {
        while (current != NodeNullIndex) {
            stack.push_back(current);
            current = NodeAt(*this, current)->Left;
        }
        current = stack.back();
        stack.pop_back();

        const TNode* node = NodeAt(*this, current);
        if (func(node->Range) == EEnumerateContinuation::Stop) {
            return;
        }
        current = node->Right;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldSet::Empty() const
{
    return Root_ == NodeNullIndex;
}

size_t TBlockRangeFieldSet::GetBlockCount() const
{
    size_t total = 0;
    Enumerate(
        [&](TRange r)
        {
            total += r.Size();
            return EEnumerateContinuation::Continue;
        });
    return total;
}

bool TBlockRangeFieldSet::OutOfMemory() const
{
    return OutOfMemory_;
}

size_t TBlockRangeFieldSet::GetSegmentCount() const
{
    size_t count = 0;
    Enumerate(
        [&](TRange)
        {
            ++count;
            return EEnumerateContinuation::Continue;
        });
    return count;
}

TString TBlockRangeFieldSet::Print() const
{
    TStringBuilder sb;
    Enumerate(
        [&](TRange r)
        {
            sb << r.Print();
            return EEnumerateContinuation::Continue;
        });
    return sb;
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
