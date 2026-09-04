#pragma once

#include "block_range_field_impl.h"

#include <util/generic/set.h>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

// Stores normalized block ranges in a set ordered by their end index.
class TBlockRangeFieldStdSet: public IBlockRangeFieldImpl
{
public:
    explicit TBlockRangeFieldStdSet(ui16 maxBlockCount = Max<ui16>());

    bool TryAdd(TBlockRange16 range, bool* changed) override;
    bool TryRemove(TBlockRange16 range, bool* changed) override;
    void Clear() override;

    [[nodiscard]] bool Overlaps(TBlockRange16 other) const override;
    void Enumerate(TEnumerateFunc func) const override;

    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] ui16 GetBlockCount() const override;
    [[nodiscard]] TString Print() const override;

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
