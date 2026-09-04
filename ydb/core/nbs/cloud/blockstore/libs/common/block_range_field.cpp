#include "block_range_field.h"

#include <util/string/builder.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

TBlockRangeField::TBlockRangeField(ui16 maxBlockCount)
    : MaxBlockCount(maxBlockCount)
{}

bool TBlockRangeField::Add(TBlockRange16 range)
{
    return Add(TBlockRange64::MakeClosedInterval(range.Start, range.End));
}

bool TBlockRangeField::Add(TBlockRange64 range)
{
    Y_ABORT_UNLESS(range.End < MaxBlockCount);
    const TBlockRange16 range16 = ConvertRangeSafe16(range);

    // Non-overlapping ranges sorted by End are also sorted by Start, so we
    // can iterate forward and stop early.

    // Find first existing interval with End >= range.Start - 1 (adjacent or
    // overlapping on the left side of the new range).
    // When range.Start == 0, "range.Start - 1" would underflow → start from
    // begin() to cover all intervals.
    auto it = (range16.Start > 0)
                  ? Intervals.lower_bound(
                        TBlockRange16::MakeClosedInterval(0, range16.Start - 1))
                  : Intervals.begin();

    ui16 mergedStart = range16.Start;
    ui16 mergedEnd = range16.End;
    size_t erasedCount = 0;
    TBlockRange16 firstErased = range16;

    while (it != Intervals.end()) {
        // For non-overlapping ranges sorted by End (= sorted by Start), we can
        // stop when the next interval starts strictly after mergedEnd + 1.
        // Guard against overflow when mergedEnd == MaxIndex: in that case every
        // possible Start is <= mergedEnd, so no early exit is possible.
        if (mergedEnd != TBlockRange16::MaxIndex && it->Start > mergedEnd + 1) {
            break;
        }

        if (erasedCount == 0) {
            firstErased = *it;
        }
        mergedStart = Min(mergedStart, it->Start);
        mergedEnd = Max(mergedEnd, it->End);
        it = Intervals.erase(it);
        ++erasedCount;
    }

    const TBlockRange16 merged =
        TBlockRange16::MakeClosedInterval(mergedStart, mergedEnd);
    Intervals.insert(merged);

    return erasedCount != 1 || merged != firstErased;
}

bool TBlockRangeField::Add(const TBlockRangeField& field)
{
    if (this == &field) {
        return false;
    }

    bool changed = false;
    for (const auto& range: field.Intervals) {
        changed |=
            Add(TBlockRange64::MakeClosedInterval(range.Start, range.End));
    }
    return changed;
}

bool TBlockRangeField::Remove(TBlockRange16 range)
{
    return Remove(TBlockRange64::MakeClosedInterval(range.Start, range.End));
}

bool TBlockRangeField::Remove(TBlockRange64 range)
{
    if (Intervals.empty()) {
        return false;
    }

    // Find first interval with End >= range.Start (could overlap with range).
    if (range.Start >= MaxBlockCount) {
        return false;
    }

    const TBlockRange16 range16 =
        ConvertRangeSafe16(TBlockRange64::MakeClosedInterval(
            range.Start,
            Min<ui64>(range.End, MaxBlockCount - 1)));
    auto it = Intervals.lower_bound(
        TBlockRange16::MakeClosedInterval(0, range16.Start));

    bool changed = false;
    while (it != Intervals.end()) {
        // Since Start is monotonically increasing (non-overlapping + sorted by
        // End), stop once Start is past range.End.
        if (it->Start > range16.End) {
            break;
        }

        const TBlockRange16 existing = *it;
        it = Intervals.erase(it);
        changed = true;

        // Keep the left tail if the existing interval starts before
        // range.Start.
        if (existing.Start < range16.Start) {
            Intervals.insert(TBlockRange16::MakeClosedInterval(
                existing.Start,
                range16.Start - 1));
        }

        // Keep the right tail if the existing interval ends after range.End.
        if (existing.End > range16.End) {
            Intervals.insert(TBlockRange16::MakeClosedInterval(
                range16.End + 1,
                existing.End));
            break;
        }
    }
    return changed;
}

bool TBlockRangeField::Remove(const TBlockRangeField& field)
{
    if (this == &field) {
        return Clear();
    }

    bool changed = false;
    for (const auto& range: field.Intervals) {
        changed |=
            Remove(TBlockRange64::MakeClosedInterval(range.Start, range.End));
    }
    return changed;
}

bool TBlockRangeField::Clear()
{
    const bool changed = !Intervals.empty();
    Intervals.clear();
    return changed;
}

bool TBlockRangeField::Overlaps(TBlockRange16 other) const
{
    return Overlaps(TBlockRange64::MakeClosedInterval(other.Start, other.End));
}

bool TBlockRangeField::Overlaps(TBlockRange64 other) const
{
    if (Intervals.empty()) {
        return false;
    }

    // First interval with End >= other.Start.
    if (other.Start >= MaxBlockCount) {
        return false;
    }

    const TBlockRange16 other16 =
        ConvertRangeSafe16(TBlockRange64::MakeClosedInterval(
            other.Start,
            Min<ui64>(other.End, MaxBlockCount - 1)));
    auto it = Intervals.lower_bound(
        TBlockRange16::MakeClosedInterval(0, other16.Start));

    if (it == Intervals.end()) {
        return false;
    }

    return it->Overlaps(other16);
}

bool TBlockRangeField::Overlaps(const TBlockRangeField& other) const
{
    // Disjoint intervals are ordered by both Start and End, so advance the
    // interval that lies entirely before the other one.
    auto left = Intervals.begin();
    auto right = other.Intervals.begin();

    while (left != Intervals.end() && right != other.Intervals.end()) {
        if (left->End < right->Start) {
            ++left;
        } else if (right->End < left->Start) {
            ++right;
        } else {
            return true;
        }
    }

    return false;
}

void TBlockRangeField::Enumerate(TEnumerateFunc func) const
{
    for (const auto& range: Intervals) {
        if (func(range) == EEnumerateContinuation::Stop) {
            break;
        }
    }
}

bool TBlockRangeField::Empty() const
{
    return Intervals.empty();
}

size_t TBlockRangeField::GetBlockCount() const
{
    size_t total = 0;
    for (const auto& range: Intervals) {
        total += range.Size();
    }
    return total;
}

size_t TBlockRangeField::GetSegmentCount() const
{
    return Intervals.size();
}

TString TBlockRangeField::Print() const
{
    TStringBuilder result;
    for (const auto& range: Intervals) {
        result << range.Print();
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
