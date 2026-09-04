#pragma once

#include "block_range.h"

#include <util/generic/string.h>
#include <util/string/builder.h>

#include <functional>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

class IBlockRangeFieldImpl
{
public:
    enum class ERealization
    {
        BitMaskBased,
        NodeBased,
    };
    enum class EEnumerateContinuation
    {
        Continue,
        Stop,
    };
    using TRange = TBlockRange16;
    using TEnumerateFunc = std::function<EEnumerateContinuation(TRange item)>;

    virtual ~IBlockRangeFieldImpl() = default;

    [[nodiscard]] virtual ERealization GetRealization() const = 0;

    virtual bool TryAdd(TRange range, bool* changed) = 0;
    virtual bool TryRemove(TRange range, bool* changed) = 0;
    virtual void Clear() = 0;

    [[nodiscard]] virtual bool Overlaps(TRange range) const = 0;
    [[nodiscard]] virtual bool Empty() const = 0;
    [[nodiscard]] virtual size_t GetBlockCount() const = 0;

    // Should be implemented for NodeBased realizations.
    virtual void Enumerate(TEnumerateFunc func) const = 0;
    // Should be implemented for NodeBased realizations.
    [[nodiscard]] virtual size_t GetSegmentCount() const = 0;
    // Should be implemented for BitMaskBased realizations.
    virtual void Serialize(TString* out) const;

    [[nodiscard]] virtual TString Print() const;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
