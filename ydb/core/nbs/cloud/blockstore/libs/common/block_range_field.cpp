#include "block_range_field.h"

#include "block_range_allocator.h"

#include <util/generic/set.h>
#include <util/string/builder.h>

#include <memory>

namespace NYdb::NBS::NBlockStore {

namespace {

////////////////////////////////////////////////////////////////////////////////

struct TEndComparator
{
    bool operator()(TBlockRange64 a, TBlockRange64 b) const
    {
        return a.End < b.End;
    }
};

using TAllocator = TBlockRangeFieldAllocator<TBlockRange64>;
using TIntervalSet = TSet<TBlockRange64, TEndComparator, TAllocator>;

}   // namespace

////////////////////////////////////////////////////////////////////////////////

struct TBlockRangeField::TImpl
{
    explicit TImpl(size_t poolSize)
        : Pool(poolSize)
        , Intervals(TEndComparator(), TAllocator(&Pool))
    {}

    // Pool must be declared before Intervals so that it is fully constructed
    // when the set's allocator is initialized.
    TBlockRangePool Pool;
    TIntervalSet Intervals;
};

////////////////////////////////////////////////////////////////////////////////

TBlockRangeField::TBlockRangeField(size_t poolSize)
    : Impl_(std::make_unique<TImpl>(poolSize))
{}

TBlockRangeField::~TBlockRangeField() = default;

TBlockRangeField::TBlockRangeField(TBlockRangeField&& other) noexcept = default;

TBlockRangeField& TBlockRangeField::operator=(
    TBlockRangeField&& other) noexcept = default;

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeField::Add(TBlockRange64 range)
{
    auto& intervals = Impl_->Intervals;

    // Non-overlapping ranges sorted by End are also sorted by Start, so we
    // can iterate forward and stop early.

    // Find first existing interval with End >= range.Start - 1 (adjacent or
    // overlapping on the left side of the new range).
    // When range.Start == 0, "range.Start - 1" would underflow → start from
    // begin() to cover all intervals.
    auto it = (range.Start > 0)
                  ? intervals.lower_bound(
                        TBlockRange64::MakeClosedInterval(0, range.Start - 1))
                  : intervals.begin();

    ui64 mergedStart = range.Start;
    ui64 mergedEnd = range.End;
    size_t erasedCount = 0;
    TBlockRange64 firstErased = range;

    while (it != intervals.end()) {
        // For non-overlapping ranges sorted by End (= sorted by Start), we can
        // stop when the next interval starts strictly after mergedEnd + 1.
        // Guard against overflow when mergedEnd == MaxIndex: in that case every
        // possible Start is <= mergedEnd, so no early exit is possible.
        if (mergedEnd != TBlockRange64::MaxIndex && it->Start > mergedEnd + 1) {
            break;
        }

        if (erasedCount == 0) {
            firstErased = *it;
        }
        mergedStart = Min(mergedStart, it->Start);
        mergedEnd = Max(mergedEnd, it->End);
        it = intervals.erase(it);
        ++erasedCount;
    }

    const TBlockRange64 merged =
        TBlockRange64::MakeClosedInterval(mergedStart, mergedEnd);
    intervals.insert(merged);

    return erasedCount != 1 || merged != firstErased;
}

bool TBlockRangeField::Add(const TBlockRangeField& field)
{
    if (this == &field) {
        return false;
    }

    bool changed = false;
    for (const auto& range: field.Impl_->Intervals) {
        changed |= Add(range);
    }
    return changed;
}

bool TBlockRangeField::Remove(TBlockRange64 range)
{
    auto& intervals = Impl_->Intervals;

    if (intervals.empty()) {
        return false;
    }

    // Find first interval with End >= range.Start (could overlap with range).
    auto it = intervals.lower_bound(
        TBlockRange64::MakeClosedInterval(0, range.Start));

    bool changed = false;
    while (it != intervals.end()) {
        // Since Start is monotonically increasing (non-overlapping + sorted by
        // End), stop once Start is past range.End.
        if (it->Start > range.End) {
            break;
        }

        const TBlockRange64 existing = *it;
        it = intervals.erase(it);
        changed = true;

        // Keep the left tail if the existing interval starts before
        // range.Start.
        if (existing.Start < range.Start) {
            intervals.insert(TBlockRange64::MakeClosedInterval(
                existing.Start,
                range.Start - 1));
        }

        // Keep the right tail if the existing interval ends after range.End.
        if (existing.End > range.End) {
            intervals.insert(
                TBlockRange64::MakeClosedInterval(range.End + 1, existing.End));
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
    for (const auto& range: field.Impl_->Intervals) {
        changed |= Remove(range);
    }
    return changed;
}

bool TBlockRangeField::Clear()
{
    auto& intervals = Impl_->Intervals;

    const bool changed = !intervals.empty();
    intervals.clear();
    return changed;
}

bool TBlockRangeField::Overlaps(TBlockRange64 other) const
{
    const auto& intervals = Impl_->Intervals;

    if (intervals.empty()) {
        return false;
    }

    // First interval with End >= other.Start.
    auto it = intervals.lower_bound(
        TBlockRange64::MakeClosedInterval(0, other.Start));

    if (it == intervals.end()) {
        return false;
    }

    return it->Overlaps(other);
}

bool TBlockRangeField::Overlaps(const TBlockRangeField& other) const
{
    // Disjoint intervals are ordered by both Start and End, so advance the
    // interval that lies entirely before the other one.
    auto left = Impl_->Intervals.begin();
    auto right = other.Impl_->Intervals.begin();

    while (left != Impl_->Intervals.end() &&
           right != other.Impl_->Intervals.end())
    {
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
    for (const auto& range: Impl_->Intervals) {
        if (func(range) == EEnumerateContinuation::Stop) {
            break;
        }
    }
}

bool TBlockRangeField::Empty() const
{
    return Impl_->Intervals.empty();
}

size_t TBlockRangeField::GetBlockCount() const
{
    size_t total = 0;
    for (const auto& range: Impl_->Intervals) {
        total += range.Size();
    }
    return total;
}

size_t TBlockRangeField::GetSegmentCount() const
{
    return Impl_->Intervals.size();
}

TString TBlockRangeField::Print() const
{
    TStringBuilder result;
    for (const auto& range: Impl_->Intervals) {
        result << range.Print();
    }
    return result;
}

size_t TBlockRangeField::GetUsedBytes() const
{
    return Impl_->Pool.GetUsedBytes();
}

size_t TBlockRangeField::GetPoolSize() const
{
    return Impl_->Pool.GetPoolSize();
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
