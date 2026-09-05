#pragma once

#include "block_range.h"

#include <util/generic/string.h>

#include <functional>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// Defines operations supported by block-range field implementations.
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
    using TEnumerateFunc =
        std::function<EEnumerateContinuation(TBlockRange16 item)>;

    virtual ~IBlockRangeFieldImpl() = default;

    [[nodiscard]] virtual ERealization GetRealization() const = 0;

    virtual bool TryAdd(TBlockRange16 range, bool* changed) = 0;
    virtual bool TryRemove(TBlockRange16 range, bool* changed) = 0;
    virtual void Clear() = 0;

    // Returns true when the field overlaps the range.
    [[nodiscard]] virtual bool Overlaps(TBlockRange16 other) const = 0;

    // Enumerates ranges until the callback requests a stop.
    virtual void Enumerate(TEnumerateFunc func) const = 0;

    [[nodiscard]] virtual bool Empty() const = 0;
    [[nodiscard]] virtual size_t GetBlockCount() const = 0;

    // Should be implemented for NodeBased realizations.
    [[nodiscard]] virtual size_t GetSegmentCount() const = 0;

    virtual void Serialize(TString* out) const = 0;
    [[nodiscard]] virtual TString Print() const = 0;
};

//////////////////////////////////////////////////////////////////////////////

class TNodeBasedBlockRangeFieldBase: public IBlockRangeFieldImpl
{
public:
    ERealization GetRealization() const final;
    void Serialize(TString* out) const final;
    TString Print() const final;
};

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
