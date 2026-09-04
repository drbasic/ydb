#pragma once

#include "block_range.h"

#include <functional>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

// Defines operations supported by block-range field implementations.
class IBlockRangeFieldImpl
{
public:
    enum class EEnumerateContinuation
    {
        Continue,
        Stop,
    };
    using TEnumerateFunc =
        std::function<EEnumerateContinuation(TBlockRange16 item)>;

    virtual ~IBlockRangeFieldImpl() = default;

    virtual bool TryAdd(TBlockRange16 range, bool* changed) = 0;
    virtual bool TryRemove(TBlockRange16 range, bool* changed) = 0;
    virtual void Clear() = 0;

    // Returns true when the field overlaps the range.
    [[nodiscard]] virtual bool Overlaps(TBlockRange16 other) const = 0;

    // Enumerates ranges until the callback requests a stop.
    virtual void Enumerate(TEnumerateFunc func) const = 0;

    [[nodiscard]] virtual bool Empty() const = 0;
    [[nodiscard]] virtual ui16 GetBlockCount() const = 0;

    // Returns a human-readable representation of all ranges.
    [[nodiscard]] virtual TString Print() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
