#include "block_range_field_set.h"

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/builder.h>

#include <new>

namespace NYdb::NBS::NBlockStore {

namespace {

////////////////////////////////////////////////////////////////////////////////

constexpr ui16 NodeNullIndex = 0xFFFF;
constexpr size_t NodeSize = 8;
constexpr size_t ArenaSize = 512;
constexpr size_t MaxNodeCount = ArenaSize / NodeSize;

////////////////////////////////////////////////////////////////////////////////

}   // namespace

////////////////////////////////////////////////////////////////////////////////
struct TBlockRangeFieldSet::TNode
{
    TBlockRange<ui16> Range;
    ui16 Left = NodeNullIndex;
    ui16 Right = NodeNullIndex;

    void Init()
    {
        Left = NodeNullIndex;
        Right = NodeNullIndex;
    }
};

static_assert(NodeSize == sizeof(TBlockRangeFieldSet::TNode));

////////////////////////////////////////////////////////////////////////////////

TBlockRangeFieldSet::TBlockRangeFieldSet()
    : Root(NodeNullIndex)
    , FreeHead(NodeNullIndex)
{
    Arena = static_cast<char*>(::operator new(ArenaSize));
}

TBlockRangeFieldSet::~TBlockRangeFieldSet()
{
    ::operator delete(Arena);
}

IBlockRangeFieldImpl::EBackend TBlockRangeFieldSet::GetBackend() const
{
    return EBackend::Set;
}

bool TBlockRangeFieldSet::TryAdd(TRange range, bool* changed)
{
    *changed = false;

    if (Root == NodeNullIndex) {
        // Empty tree: plain insertion.
        const ui16 idx = AllocNode();
        if (idx == NodeNullIndex) {
            Y_ABORT_UNLESS(false);
            return false;
        }
        GetNode(idx)->Range = range;
        InsertNode(idx);
        *changed = true;
        return true;
    }

    // Find the first node overlapping or adjacent to the range:
    // either the predecessor touching us from the left,
    // or the lower bound node touching us from the right.
    const ui16 pred = FindPredecessorNode(range.Start);
    const ui16 lower = FindLowerBoundNode(range.Start);

    ui16 first = NodeNullIndex;
    if (pred != NodeNullIndex &&
        static_cast<ui32>(GetNode(pred)->Range.End) + 1 >= range.Start)
    {
        first = pred;
    } else if (
        lower != NodeNullIndex &&
        static_cast<ui32>(GetNode(lower)->Range.Start) <= range.End + 1)
    {
        first = lower;
    }

    if (first == NodeNullIndex) {
        // No overlap and no adjacency: plain insertion.
        const ui16 idx = AllocNode();
        if (idx == NodeNullIndex) {
            return false;
        }
        GetNode(idx)->Range = range;
        InsertNode(idx);
        *changed = true;
        return true;
    }

    const TRange firstRange = GetNode(first)->Range;

    // Fast path: fully covered by an existing range — nothing changes.
    if (firstRange.Start <= range.Start && range.End <= firstRange.End) {
        return true;
    }

    // Compute the merged range in local variables.
    // Do NOT modify any node's Start in-place: it would break the BST.
    // Instead, we remove all affected nodes first, then insert one merged node.
    ui16 mergedStart = Min(range.Start, firstRange.Start);
    ui16 mergedEnd = Max(range.End, firstRange.End);

    // Remove first node from the tree.
    RemoveNodeByKey(firstRange.Start);

    // Walk right from mergedStart, remove nodes that are covered or adjacent,
    // expanding mergedEnd as we go. Stop at the first gap.
    ui16 current = FindNextGreaterNode(mergedStart);
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
        if (node->Range.Start > static_cast<ui32>(mergedEnd) + 1) {
            // Gap found — the rest of the tree is guaranteed non-adjacent.
            break;
        }
        // This node is covered or adjacent: widen and remove.
        mergedEnd = Max(mergedEnd, node->Range.End);
        const ui16 nextStart = node->Range.Start;
        RemoveNodeByKey(nextStart);
        current = FindNextGreaterNode(mergedStart);
    }

    // Re-insert with the merged range.
    *changed = true;
    const ui16 allocIdx = AllocNode();
    if (allocIdx == NodeNullIndex) {
        return false;
    }
    GetNode(allocIdx)->Range.Start = mergedStart;
    GetNode(allocIdx)->Range.End = mergedEnd;
    InsertNode(allocIdx);

    return true;
}

bool TBlockRangeFieldSet::TryRemove(TRange range, bool* changed)
{
    *changed = false;
    if (Root == NodeNullIndex) {
        return true;
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
        ui16 current = Root;
        bool found = false;
        while (current != NodeNullIndex) {
            const TNode* node = GetNode(current);
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
            return true;
        }
    }

    // Find the first node overlapping the range (adjacency does not count).
    const ui16 pred = FindPredecessorNode(range.Start);
    const ui16 lower = FindLowerBoundNode(range.Start);

    ui16 current = NodeNullIndex;
    if (pred != NodeNullIndex && GetNode(pred)->Range.End >= range.Start) {
        current = pred;
    } else if (
        lower != NodeNullIndex && GetNode(lower)->Range.Start <= range.End)
    {
        current = lower;
    } else {
        Y_ABORT_UNLESS(false);
        return true;   // Unreachable after the fast path check.
    }

    // Process the run of overlapping nodes from left to right.
    while (current != NodeNullIndex &&
           GetNode(current)->Range.Start <= range.End)
    {
        const ui16 nodeStart = GetNode(current)->Range.Start;
        const ui16 nodeEnd = GetNode(current)->Range.End;

        if (nodeStart < range.Start && nodeEnd > range.End) {
            // The range cuts a hole inside this node: trim the left part
            // and insert the right part as a new node.
            const ui16 idx = AllocNode();
            if (idx == NodeNullIndex) {
                return false;
            }
            *changed = true;
            // Update block count: remove the trimmed portion from the left
            // node.
            const size_t oldNodeSize = GetNode(current)->Range.Size();
            GetNode(current)->Range.End = range.Start > 0 ? range.Start - 1 : 0;
            BlockCount -= (oldNodeSize - GetNode(current)->Range.Size());
            GetNode(idx)->Range =
                TRange::MakeClosedInterval(range.End + 1, nodeEnd);
            InsertNode(idx);
            break;
        }
        if (nodeStart < range.Start) {
            // Overlap on the left: trim the right edge in place.
            // The key (Start) does not change, the order is preserved.
            const size_t oldNodeSize = GetNode(current)->Range.Size();
            GetNode(current)->Range.End = range.Start > 0 ? range.Start - 1 : 0;
            BlockCount -= (oldNodeSize - GetNode(current)->Range.Size());
            *changed = true;
            current = FindNextGreaterNode(nodeStart);
            continue;
        }
        if (nodeEnd > range.End) {
            // Overlap on the right: trim the left edge in place.
            // The new Start is still greater than the previous node's End
            // and less than the next node's Start, so the order holds.
            const size_t oldNodeSize = GetNode(current)->Range.Size();
            GetNode(current)->Range.Start =
                range.End < Max<ui16>() ? range.End + 1 : Max<ui16>();
            BlockCount -= (oldNodeSize - GetNode(current)->Range.Size());
            *changed = true;
            break;
        }
        // Fully covered: remove and continue with the next node.
        current = FindNextGreaterNode(nodeStart);
        RemoveNodeByKey(nodeStart);
        *changed = true;
    }

    return true;
}

void TBlockRangeFieldSet::Clear()
{
    Root = NodeNullIndex;
    FreeHead = NodeNullIndex;
    UsedCount = 0;
    SegmentCount = 0;
    BlockCount = 0;
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldSet::Overlaps(TRange other) const
{
    if (Root == NodeNullIndex) {
        return false;
    }

    ui16 current = Root;
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
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

void TBlockRangeFieldSet::Enumerate(TEnumerateFunc func) const
{
    if (Root == NodeNullIndex) {
        return;
    }

    ui16 current = Root;
    TVector<ui16> stack;

    while (current != NodeNullIndex || !stack.empty()) {
        while (current != NodeNullIndex) {
            stack.push_back(current);
            current = GetNode(current)->Left;
        }
        current = stack.back();
        stack.pop_back();

        const TNode* node = GetNode(current);
        if (func(node->Range) == EEnumerateContinuation::Stop) {
            return;
        }
        current = node->Right;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldSet::Empty() const
{
    return Root == NodeNullIndex;
}

size_t TBlockRangeFieldSet::GetBlockCount() const
{
    return IntegerCast<ui16>(BlockCount);
}

size_t TBlockRangeFieldSet::GetSegmentCount() const
{
    return SegmentCount;
}

ui16 TBlockRangeFieldSet::AllocNode()
{
    if (FreeHead != NodeNullIndex) {
        ui16 idx = FreeHead;
        FreeHead = GetNode(idx)->Left;
        GetNode(idx)->Init();
        ++SegmentCount;
        return idx;
    }
    if (UsedCount >= MaxNodeCount) {
        return NodeNullIndex;
    }
    const ui16 idx = UsedCount++;
    GetNode(idx)->Init();
    ++SegmentCount;
    return idx;
}

void TBlockRangeFieldSet::FreeNode(ui16 idx)
{
    if (idx == NodeNullIndex) {
        return;
    }
    GetNode(idx)->Left = FreeHead;
    FreeHead = idx;
    --SegmentCount;
    BlockCount -= GetNode(idx)->Range.Size();
}

TBlockRangeFieldSet::TNode* TBlockRangeFieldSet::GetNode(ui16 idx)
{
    return idx == NodeNullIndex
               ? nullptr
               : reinterpret_cast<TNode*>(Arena + idx * NodeSize);
}

const TBlockRangeFieldSet::TNode* TBlockRangeFieldSet::GetNode(ui16 idx) const
{
    return idx == NodeNullIndex
               ? nullptr
               : reinterpret_cast<const TNode*>(Arena + idx * NodeSize);
}

ui16 TBlockRangeFieldSet::FindLowerBoundNode(ui16 key) const
{
    ui16 current = Root;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
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
    ui16 current = Root;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
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
    ui16 current = Root;
    ui16 best = NodeNullIndex;
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
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
    ui16 current = Root;
    parent = NodeNullIndex;
    isLeftChild = false;
    while (current != NodeNullIndex) {
        const TNode* node = GetNode(current);
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

void TBlockRangeFieldSet::InsertNode(ui16 idx)
{
    const size_t blockCount = GetNode(idx)->Range.Size();
    BlockCount += blockCount;
    if (Root == NodeNullIndex) {
        Root = idx;
    } else {
        const ui16 key = GetNode(idx)->Range.Start;
        ui16 current = Root;
        while (true) {
            TNode* curNode = GetNode(current);
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
}

void TBlockRangeFieldSet::RemoveNodeByKey(ui16 key)
{
    ui16 parent = NodeNullIndex;
    bool isLeftChild = false;
    const ui16 idx = FindNodeByKey(key, parent, isLeftChild);
    if (idx == NodeNullIndex) {
        return;
    }
    TNode* node = GetNode(idx);

    // Attach a replacement child to the parent (or make it the root).
    const auto attach = [&](ui16 replacement)
    {
        if (parent == NodeNullIndex) {
            Root = replacement;
        } else if (isLeftChild) {
            GetNode(parent)->Left = replacement;
        } else {
            GetNode(parent)->Right = replacement;
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
    while (GetNode(successor)->Left != NodeNullIndex) {
        successorParent = successor;
        successor = GetNode(successor)->Left;
    }
    TNode* succNode = GetNode(successor);
    node->Range = succNode->Range;
    if (successorParent == idx) {
        node->Right = succNode->Right;
    } else {
        GetNode(successorParent)->Left = succNode->Right;
    }
    FreeNode(successor);
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
