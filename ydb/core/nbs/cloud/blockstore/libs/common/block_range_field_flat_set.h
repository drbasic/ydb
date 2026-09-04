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
    : public IBlockRangeFieldImpl
    , public TDisableCopy
{
public:
    explicit TBlockRangeFieldFlatSet(ui16 maxSize);

    ERealization GetRealization() const override;
    bool TryAdd(TRange range, bool* changed) override;
    bool TryRemove(TRange range, bool* changed) override;

    void Clear() override;

    [[nodiscard]] bool Overlaps(TRange other) const override;

    void Enumerate(TEnumerateFunc func) const override;

    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] size_t GetBlockCount() const override;

    [[nodiscard]] size_t GetSegmentCount() const override;

private:
    void EnsureCapacity();

    TVector<TRange> Ranges;
    ui32 RangeCount = 0;
    ui32 BlockCount = 0;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
