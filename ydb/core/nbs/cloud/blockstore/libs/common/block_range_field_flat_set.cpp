#include "block_range_field_flat_set.h"

namespace NYdb::NBS::NBlockStore {

namespace {}   // namespace

////////////////////////////////////////////////////////////////////////////////

TBlockRangeFieldFlatSet::TBlockRangeFieldFlatSet(ui16 maxSize)
{
    Ranges.resize(maxSize);
}

////////////////////////////////////////////////////////////////////////////////

IBlockRangeFieldImpl::ERealization
TBlockRangeFieldFlatSet::GetRealization() const
{
    return ERealization::NodeBased;
}

// Нужно найти бинарным поиском в векторе индекс range.Start-1, range.end+1.

bool TBlockRangeFieldFlatSet::TryAdd(TRange range, bool* changed)
{
    Y_UNUSED(range, changed);
    return false;
}

bool TBlockRangeFieldFlatSet::TryRemove(TRange range, bool* changed)
{
    Y_UNUSED(range, changed);
    return false;
}

void TBlockRangeFieldFlatSet::Clear()
{
    RangeCount = 0;
    BlockCount = 0;
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldFlatSet::Overlaps(TRange other) const
{
    Y_UNUSED(other);
    return false;
}

void TBlockRangeFieldFlatSet::Enumerate(TEnumerateFunc func) const
{
    for (ui32 i = 0; i < RangeCount; ++i) {
        if (func(Ranges[i]) == EEnumerateContinuation::Stop) {
            return;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldFlatSet::Empty() const
{
    return RangeCount == 0;
}

size_t TBlockRangeFieldFlatSet::GetBlockCount() const
{
    return BlockCount;
}

size_t TBlockRangeFieldFlatSet::GetSegmentCount() const
{
    return RangeCount;
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
