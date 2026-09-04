#pragma once

#include "block_range_field_impl.h"

#include <optional>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

// Stores at most one continuous block range without heap allocations.
class TBlockRangeFieldSimple final: public IBlockRangeFieldImpl
{
public:
    bool TryAdd(TBlockRange16 range, bool* changed) override;
    bool TryRemove(TBlockRange16 range, bool* changed) override;
    void Clear() override;

    [[nodiscard]] bool Overlaps(TBlockRange16 other) const override;
    void Enumerate(TEnumerateFunc func) const override;
    [[nodiscard]] bool Empty() const override;
    [[nodiscard]] ui16 GetBlockCount() const override;
    [[nodiscard]] TString Print() const override;

private:
    std::optional<TBlockRange16> Range;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
