#pragma once

#include "public.h"

#include "block_range.h"

#include <util/generic/set.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

class TBlockRangeField
{
public:
    enum class EEnumerateContinuation
    {
        Continue,
        Stop,
    };
    using TEnumerateFunc =
        std::function<EEnumerateContinuation(TBlockRange16 item)>;

    explicit TBlockRangeField(ui16 maxBlockCount = Max<ui16>());

    // Returns true if the intervals have actually changed.
    bool Add(TBlockRange16 range);
    // Returns true if the intervals have actually changed.
    bool Add(TBlockRange64 range);
    // Returns true if the intervals have actually changed.
    bool Add(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Remove(TBlockRange16 range);
    // Returns true if the intervals have actually changed.
    bool Remove(TBlockRange64 range);
    // Returns true if the intervals have actually changed.
    bool Remove(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Clear();

    [[nodiscard]] bool Overlaps(TBlockRange16 other) const;
    [[nodiscard]] bool Overlaps(TBlockRange64 other) const;
    [[nodiscard]] bool Overlaps(const TBlockRangeField& other) const;

    void Enumerate(TEnumerateFunc func) const;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] size_t GetBlockCount() const;
    [[nodiscard]] size_t GetSegmentCount() const;
    [[nodiscard]] TString Print() const;

private:
    struct TBlockRangeComparator
    {
        bool operator()(TBlockRange16 a, TBlockRange16 b) const
        {
            return a.End < b.End;
        }
    };

    const ui16 MaxBlockCount;
    TSet<TBlockRange16, TBlockRangeComparator> Intervals;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
