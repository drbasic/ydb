#include "block_range_field_flat_set.h"

#include <util/generic/cast.h>

#include <cstring>

namespace NYdb::NBS::NBlockStore {

namespace {

////////////////////////////////////////////////////////////////////////////////

// Returns true when the entry starts not further than one block past the
// given end (i.e. it overlaps or touches the range ending at |end|).
bool StartsWithinTouchDistance(const TBlockRange16& entry, ui32 end)
{
    return static_cast<ui32>(entry.Start) <= end + 1;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

TBlockRangeFieldFlatSet::TBlockRangeFieldFlatSet(ui16 maxSegmentCount)
    : MaxSegmentCount(maxSegmentCount)
{}

////////////////////////////////////////////////////////////////////////////////

size_t TBlockRangeFieldFlatSet::LowerBound(ui16 key) const
{
    size_t lo = 0;
    size_t hi = RangeCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (Ranges[mid].Start < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void TBlockRangeFieldFlatSet::EnsureCapacity()
{
    if (Ranges.empty()) {
        Ranges.resize(MaxSegmentCount);
    }
}

IBlockRangeFieldImpl::EBackend TBlockRangeFieldFlatSet::GetBackend() const
{
    return EBackend::FlatSet;
}

bool TBlockRangeFieldFlatSet::TryAdd(TRange range, bool* changed)
{
    *changed = false;
    if (OutOfMemory_) {
        return false;
    }
    EnsureCapacity();

    // Find the first entry that overlaps the range or touches it from the
    // left (its End + 1 >= range.Start).
    size_t first = LowerBound(range.Start);
    if (first > 0 &&
        static_cast<ui32>(Ranges[first - 1].End) + 1 >= range.Start)
    {
        --first;
    }

    // Walk right collecting all entries that overlap or touch the range.
    ui32 mergedStart = range.Start;
    ui32 mergedEnd = range.End;
    size_t last = first;
    while (last < RangeCount &&
           StartsWithinTouchDistance(Ranges[last], mergedEnd))
    {
        mergedStart = Min<ui32>(mergedStart, Ranges[last].Start);
        mergedEnd = Max<ui32>(mergedEnd, Ranges[last].End);
        ++last;
    }

    // Fully covered by a single existing entry: nothing changes.
    if (last - first == 1 && Ranges[first].Start == mergedStart &&
        Ranges[first].End == mergedEnd)
    {
        return false;
    }

    const size_t erasedCount = last - first;
    const size_t newRangeCount = RangeCount - erasedCount + 1;
    if (newRangeCount > MaxSegmentCount) {
        OutOfMemory_ = true;
        return false;
    }

    ui32 erasedBlocks = 0;
    for (size_t i = first; i < last; ++i) {
        erasedBlocks += Ranges[i].Size();
    }

    // Shift the tail to make room for the merged entry / close the gap.
    if (erasedCount > 1) {
        memmove(
            &Ranges[first + 1],
            &Ranges[last],
            (RangeCount - last) * sizeof(TRange));
    } else if (erasedCount == 0) {
        memmove(
            &Ranges[first + 1],
            &Ranges[first],
            (RangeCount - first) * sizeof(TRange));
    }

    Ranges[first] = TBlockRange16::MakeClosedInterval(
        IntegerCast<ui16>(mergedStart),
        IntegerCast<ui16>(mergedEnd));
    RangeCount = IntegerCast<ui32>(newRangeCount);
    BlockCount += mergedEnd - mergedStart + 1 - erasedBlocks;

    *changed = true;
    return true;
}

bool TBlockRangeFieldFlatSet::TryRemove(TRange range, bool* changed)
{
    *changed = false;
    if (RangeCount == 0) {
        return false;
    }

    // Find the first entry that overlaps the range (adjacency does not
    // count): either the entry at the lower bound or its predecessor.
    size_t idx = LowerBound(range.Start);
    if (idx == RangeCount || Ranges[idx].Start > range.End) {
        if (idx == 0 || Ranges[idx - 1].End < range.Start) {
            // Nothing overlaps the range.
            return false;
        }
        --idx;
    }

    // Walk the run of overlapping entries computing the remaining tails.
    bool hasLeftTail = false;
    ui32 leftTailStart = 0;
    ui32 leftTailEnd = 0;
    bool hasRightTail = false;
    ui32 rightTailStart = 0;
    ui32 rightTailEnd = 0;

    size_t last = idx;
    ui32 removedBlocks = 0;
    while (last < RangeCount && Ranges[last].Start <= range.End) {
        const TRange& entry = Ranges[last];
        removedBlocks += entry.Size();
        if (entry.Start < range.Start) {
            hasLeftTail = true;
            leftTailStart = entry.Start;
            leftTailEnd = range.Start - 1;
        }
        if (entry.End > range.End) {
            hasRightTail = true;
            rightTailStart = range.End + 1;
            rightTailEnd = entry.End;
        }
        ++last;
    }

    const size_t erasedCount = last - idx;
    const size_t newEntryCount = (hasLeftTail ? 1 : 0) + (hasRightTail ? 1 : 0);
    const size_t newRangeCount = RangeCount - erasedCount + newEntryCount;
    if (newRangeCount > MaxSegmentCount) {
        OutOfMemory_ = true;
        return false;
    }

    // Shift the tail to make room for the new entries / close the gap.
    if (newEntryCount < erasedCount) {
        memmove(
            &Ranges[idx + newEntryCount],
            &Ranges[last],
            (RangeCount - last) * sizeof(TRange));
    } else if (newEntryCount > erasedCount) {
        memmove(
            &Ranges[idx + newEntryCount],
            &Ranges[idx],
            (RangeCount - idx) * sizeof(TRange));
    }

    size_t writeIdx = idx;
    if (hasLeftTail) {
        Ranges[writeIdx++] = TBlockRange16::MakeClosedInterval(
            IntegerCast<ui16>(leftTailStart),
            IntegerCast<ui16>(leftTailEnd));
    }
    if (hasRightTail) {
        Ranges[writeIdx++] = TBlockRange16::MakeClosedInterval(
            IntegerCast<ui16>(rightTailStart),
            IntegerCast<ui16>(rightTailEnd));
    }

    RangeCount = IntegerCast<ui32>(newRangeCount);
    BlockCount -= removedBlocks;

    *changed = true;
    return true;
}

void TBlockRangeFieldFlatSet::Clear()
{
    RangeCount = 0;
    BlockCount = 0;
    OutOfMemory_ = false;
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeFieldFlatSet::Overlaps(TRange other) const
{
    if (RangeCount == 0) {
        return false;
    }

    // The first entry with Start >= other.Start either overlaps other or
    // lies entirely to the right of it. Its predecessor either overlaps
    // other or lies entirely to the left of it.
    const size_t idx = LowerBound(other.Start);
    if (idx < RangeCount && Ranges[idx].Start <= other.End) {
        return true;
    }
    return idx > 0 && Ranges[idx - 1].End >= other.Start;
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
    return IntegerCast<ui16>(BlockCount);
}

size_t TBlockRangeFieldFlatSet::GetSegmentCount() const
{
    return RangeCount;
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
