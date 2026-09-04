#pragma once

#include "public.h"

#include "block_range_field_impl.h"
#include "block_range_field_simple.h"

#include <memory>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

class TBlockRangeFieldStdSet;
class TBlockRangeFieldTestAccessor;

// Stores zero or one block range inline and uses a set for multiple ranges.
class TBlockRangeField
{
public:
    using EEnumerateContinuation = IBlockRangeFieldImpl::EEnumerateContinuation;
    using TEnumerateFunc = IBlockRangeFieldImpl::TEnumerateFunc;

    explicit TBlockRangeField(ui16 maxBlockCount = Max<ui16>());
    TBlockRangeField(const TBlockRangeField& other);
    TBlockRangeField(TBlockRangeField&& other) noexcept;
    ~TBlockRangeField();

    // Returns true if the intervals have actually changed.
    bool Add(TBlockRange16 range);
    // Returns true if the intervals have actually changed.
    bool Add(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Remove(TBlockRange16 range);
    // Returns true if the intervals have actually changed.
    bool Remove(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Clear();

    [[nodiscard]] bool Overlaps(TBlockRange16 other) const;
    [[nodiscard]] bool Overlaps(const TBlockRangeField& other) const;

    void Enumerate(TEnumerateFunc func) const;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] ui16 GetBlockCount() const;
    [[nodiscard]] size_t GetSegmentCount() const;
    [[nodiscard]] TString Print() const;

private:
    friend class TBlockRangeFieldTestAccessor;

    void CollapseImpl();

    const ui16 MaxBlockCount;
    TBlockRangeFieldSimple Simple;
    std::unique_ptr<IBlockRangeFieldImpl> Impl;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
