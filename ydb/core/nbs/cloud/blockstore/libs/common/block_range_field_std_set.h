#pragma once

#include "block_range_allocator.h"
#include "block_range_field_impl.h"

#include <util/generic/set.h>

#include <memory>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// Stores normalized block ranges in a set ordered by their end index.
// Set nodes are allocated from the provided TBlockRangePool. When no pool is
// provided, the set creates and owns its own pool.
class TBlockRangeFieldStdSet: public TNodeBasedBlockRangeFieldBase
{
public:
    // The external pool (if any) must outlive the set.
    explicit TBlockRangeFieldStdSet(
        ui16 maxBlockCount = Max<ui16>(),
        TBlockRangePool* pool = nullptr);

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

    using TAllocator = TBlockRangeFieldAllocator<TBlockRange16>;

    const ui16 MaxBlockCount;
    // Used only when no external pool is provided. Must be declared before
    // Intervals so that it is fully constructed when the set's allocator is
    // initialized.
    std::unique_ptr<TBlockRangePool> OwnPool;
    TSet<TBlockRange16, TBlockRangeComparator, TAllocator> Intervals;
};

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
