#pragma once

#include "public.h"

#include "block_range.h"

#include <memory>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

class TBlockRangeField
{
public:
    enum class EEnumerateContinuation
    {
        Continue,
        Stop,
    };
    using TEnumerateFunc =
        std::function<EEnumerateContinuation(TBlockRange64 item)>;

    explicit TBlockRangeField(size_t poolSize);
    ~TBlockRangeField();

    // Copying is forbidden: each field owns its own memory pool.
    TBlockRangeField(const TBlockRangeField&) = delete;
    TBlockRangeField& operator=(const TBlockRangeField&) = delete;

    // Movable: implementation is moved as a whole.
    TBlockRangeField(TBlockRangeField&& other) noexcept;
    TBlockRangeField& operator=(TBlockRangeField&& other) noexcept;

    // Returns true if the intervals have actually changed.
    bool Add(TBlockRange64 range);
    // Returns true if the intervals have actually changed.
    bool Add(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Remove(TBlockRange64 range);
    // Returns true if the intervals have actually changed.
    bool Remove(const TBlockRangeField& field);
    // Returns true if the intervals have actually changed.
    bool Clear();

    [[nodiscard]] bool Overlaps(TBlockRange64 other) const;
    [[nodiscard]] bool Overlaps(const TBlockRangeField& other) const;

    void Enumerate(TEnumerateFunc func) const;

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] size_t GetBlockCount() const;
    [[nodiscard]] size_t GetSegmentCount() const;
    [[nodiscard]] TString Print() const;

    // Memory pool accessors (for tests and diagnostics).
    [[nodiscard]] size_t GetUsedBytes() const;
    [[nodiscard]] size_t GetPoolSize() const;

private:
    struct TImpl;
    std::unique_ptr<TImpl> Impl_;
};

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
