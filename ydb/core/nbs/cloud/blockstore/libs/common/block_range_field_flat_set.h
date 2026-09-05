#pragma once

#include "block_range.h"
#include "block_range_field_impl.h"

#include <ydb/core/nbs/cloud/storage/core/libs/common/disable_copy.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

// Memory-optimized interval set for block ranges backed by a flat sorted
// vector.
//
// Designed for scenarios with millions of instances:
// - Stores pairwise non-overlapping, non-adjacent ranges in a sorted vector.
// - The internal vector is lazily allocated on the first Add().
// - Ranges are kept merged (adjacent/overlapping ranges are coalesced) on
//   insertion and split on removal.
// - If capacity is exceeded, Add() returns false and sets the OutOfMemory()
//   flag (no exception is propagated).
//
// Invariant: all ranges stored in the vector are pairwise non-overlapping
// and non-adjacent, sorted by Start.
//
// The caller must ensure that the number of ranges does not exceed the
// container capacity. If it is exceeded, Add() returns false and sets
// OutOfMemory().

class TBlockRangeFieldFlatSet
    : public TNodeBasedBlockRangeFieldBase
    , public TDisableCopy
{
public:
    static constexpr ui16 DefaultCapacity = 64;

    TBlockRangeFieldFlatSet()
        : TBlockRangeFieldFlatSet(DefaultCapacity)
    {}

    explicit TBlockRangeFieldFlatSet(ui16 maxSegmentCount);

    bool TryAdd(TRange range, bool* changed) override;
    bool TryRemove(TRange range, bool* changed) override;

    void Clear() override;

    [[nodiscard]] bool Overlaps(TRange other) const override;

    void Enumerate(TEnumerateFunc func) const override;

    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] size_t GetBlockCount() const override;
    [[nodiscard]] size_t GetSegmentCount() const override;

    // Returns true when a previous operation exceeded the segment capacity.
    // While the flag is set all further additions fail.
    [[nodiscard]] bool OutOfMemory() const
    {
        return OutOfMemory_;
    }

private:
    void EnsureCapacity();

    // First index of an entry with Start >= key.
    [[nodiscard]] size_t LowerBound(ui16 key) const;

    TVector<TRange> Ranges;
    const ui16 MaxSegmentCount;
    ui32 RangeCount = 0;
    ui32 BlockCount = 0;
    bool OutOfMemory_ = false;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
