#pragma once

#include "block_range_field_impl.h"

#include <ydb/core/nbs/cloud/blockstore/libs/common/memory/arena_allocator.h>
#include <ydb/core/nbs/cloud/blockstore/libs/common/memory/arena_allocator_adapter.h>

#include <util/generic/set.h>

#include <memory>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// Stores normalized block ranges in a set ordered by their end index.
// Set nodes are allocated from a TArenaAllocatorPool backed by the provided
// IArenaAllocator.
class TBlockRangeFieldStdSet: public TNodeBasedBlockRangeFieldBase
{
public:
    explicit TBlockRangeFieldStdSet(
        ui16 maxBlockCount,
        IArenaAllocatorPtr allocator);

    [[nodiscard]] EBackend GetBackend() const override;

    bool TryAdd(TBlockRange16 range, bool* changed) override;
    bool TryRemove(TBlockRange16 range, bool* changed) override;
    void Clear() override;

    [[nodiscard]] bool Overlaps(TBlockRange16 other) const override;
    void Enumerate(TEnumerateFunc func) const override;

    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] size_t GetBlockCount() const override;
    [[nodiscard]] size_t GetSegmentCount() const override;

private:
    struct TBlockRangeComparator
    {
        bool operator()(TBlockRange16 a, TBlockRange16 b) const
        {
            return a.End < b.End;
        }
    };

    using TAllocator = TArenaPoolAdapter<TBlockRange16>;

    const ui16 MaxBlockCount;
    // Must be declared before Intervals so that it outlives the set's
    // allocator.
    std::shared_ptr<TArenaAllocatorPool> Pool;
    TSet<TBlockRange16, TBlockRangeComparator, TAllocator> Intervals;
};

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
