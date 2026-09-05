#pragma once

#include "block_range.h"
#include "block_range_field_impl.h"

#include <ydb/core/nbs/cloud/storage/core/libs/common/disable_copy.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/builder.h>

#include <cstddef>
#include <functional>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

// Memory-optimized interval set for block ranges.
//
// Designed for scenarios with millions of instances:
// - Uses a custom BST backed by a 512-byte arena (64 nodes of 8 bytes each).
// - Nodes are linked by 16-bit indices instead of pointers.
// - Freed nodes are reused via a free list embedded into the nodes.
//
// Invariant: all ranges stored in the tree are pairwise non-overlapping
// and non-adjacent (they are merged on insertion), so ordering by Start
// is a total order and Start is a unique BST key.
class TBlockRangeFieldSet
    : public TNodeBasedBlockRangeFieldBase
    , public TDisableCopy
{
public:
    struct TNode;

    TBlockRangeFieldSet();
    ~TBlockRangeFieldSet() override;

    bool TryAdd(TRange range, bool* changed) override;
    bool TryRemove(TRange range, bool* changed) override;

    void Clear() override;

    [[nodiscard]] bool Overlaps(TRange other) const override;

    void Enumerate(TEnumerateFunc func) const override;

    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] size_t GetBlockCount() const override;
    [[nodiscard]] size_t GetSegmentCount() const override;

private:
    ui16 Root;
    ui16 FreeHead;
    ui16 UsedCount = 0;
    ui16 SegmentCount = 0;
    ui32 BlockCount = 0;
    char* Arena = nullptr;

    ui16 AllocNode();
    void FreeNode(ui16 idx);
    [[nodiscard]] TNode* GetNode(ui16 idx);
    [[nodiscard]] const TNode* GetNode(ui16 idx) const;

    // First node with Start >= key (lower bound), or NodeNullIndex.
    [[nodiscard]] ui16 FindLowerBoundNode(ui16 key) const;
    // First node with Start < key (predecessor), or NodeNullIndex.
    [[nodiscard]] ui16 FindPredecessorNode(ui16 key) const;
    // First node with Start > key (successor), or NodeNullIndex.
    [[nodiscard]] ui16 FindNextGreaterNode(ui16 key) const;
    // Locate node by its unique Start key; outputs parent link info.
    [[nodiscard]] ui16
    FindNodeByKey(ui16 key, ui16& parent, bool& isLeftChild) const;
    void InsertNode(ui16 idx);
    void RemoveNodeByKey(ui16 key);
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
