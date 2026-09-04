#pragma once

#include "block_range.h"

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
// - The arena is lazily allocated on the first Add() and freed on Clear().
//
// Invariant: all ranges stored in the tree are pairwise non-overlapping
// and non-adjacent (they are merged on insertion), so ordering by Start
// is a total order and Start is a unique BST key.
//
// The caller must ensure that the number of ranges does not exceed the
// arena capacity (64). If it is exceeded anyway, Add() returns false and sets
// the OutOfMemory() flag (no exception is propagated).
class TBlockRangeFieldSet
{
public:
    enum class EEnumerateContinuation
    {
        Continue,
        Stop,
    };
    using TRange = TBlockRange16;
    using TEnumerateFunc = std::function<EEnumerateContinuation(TRange item)>;

    TBlockRangeFieldSet() noexcept;
    ~TBlockRangeFieldSet();

    TBlockRangeFieldSet(const TBlockRangeFieldSet&) = delete;
    TBlockRangeFieldSet& operator=(const TBlockRangeFieldSet&) = delete;

    bool Add(TRange range);
    bool Add(const TBlockRangeFieldSet& field);

    bool Remove(TRange range);
    bool Remove(const TBlockRangeFieldSet& field);

    bool Clear();

    [[nodiscard]] bool Overlaps(TRange other) const;
    [[nodiscard]] bool Overlaps(const TBlockRangeFieldSet& other) const;

    void Enumerate(TEnumerateFunc func) const;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] size_t GetBlockCount() const;
    [[nodiscard]] bool OutOfMemory() const;

    [[nodiscard]] size_t GetSegmentCount() const;
    [[nodiscard]] TString Print() const;

private:
    static constexpr ui16 NodeNullIndex = 0xFFFF;

    ui16 Root_ = NodeNullIndex;
    ui16 FreeHead_ = NodeNullIndex;
    ui16 UsedCount_ = 0;
    bool OutOfMemory_ = false;
    bool ArenaAllocated_ = false;
    char* Arena_ = nullptr;

    void EnsureArena();
    ui16 AllocNode();
    void FreeNode(ui16 idx);
    void InsertNode(ui16 idx);

    // First node with Start >= key (lower bound), or NodeNullIndex.
    [[nodiscard]] ui16 FindLowerBoundNode(ui16 key) const;
    // First node with Start < key (predecessor), or NodeNullIndex.
    [[nodiscard]] ui16 FindPredecessorNode(ui16 key) const;
    // First node with Start > key (successor), or NodeNullIndex.
    [[nodiscard]] ui16 FindNextGreaterNode(ui16 key) const;
    // Locate node by its unique Start key; outputs parent link info.
    [[nodiscard]] ui16
    FindNodeByKey(ui16 key, ui16& parent, bool& isLeftChild) const;
    // Classic BST deletion by the unique Start key.
    void RemoveNodeByKey(ui16 key);

    friend struct TNodeHelper;
};

struct TNodeHelper
{
    static constexpr size_t NodeSize = 8;

    static void* NodeAt(TBlockRangeFieldSet& self, ui16 idx)
    {
        return const_cast<char*>(self.Arena_) + idx * NodeSize;
    }

    static const void* NodeAt(const TBlockRangeFieldSet& self, ui16 idx)
    {
        return self.Arena_ + idx * NodeSize;
    }
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
