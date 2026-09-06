#pragma once

#include "block_range_field_impl.h"
#include "block_range_field_simple.h"

#include <ydb/core/nbs/cloud/blockstore/libs/common/memory/arena_allocator.h>
#include <ydb/core/nbs/cloud/blockstore/libs/common/memory/public.h>

#include <memory>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

class TBlockRangePool;
class TBlockRangeFieldTestAccessor;

// Stores zero or one block range inline and uses the selected backend for
// multiple ranges. The pool usage can be observed via GetUsedBytes() and
// GetPoolSize().
class TBlockRangeField
{
public:
    using EBackend = IBlockRangeFieldImpl::EBackend;
    using EEnumerateContinuation = IBlockRangeFieldImpl::EEnumerateContinuation;
    using TEnumerateFunc = IBlockRangeFieldImpl::TEnumerateFunc;

    explicit TBlockRangeField(
        ui16 maxBlockCount = Max<ui16>(),
        EBackend preferredBackend = EBackend::StdSet);
    ~TBlockRangeField();

    // Copying is forbidden: each field owns its own memory pool.
    TBlockRangeField(const TBlockRangeField&) = delete;
    TBlockRangeField& operator=(const TBlockRangeField&) = delete;

    // Movable: implementation is moved as a whole.
    TBlockRangeField(TBlockRangeField&& other) noexcept;
    TBlockRangeField& operator=(TBlockRangeField&& other) noexcept;

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
    [[nodiscard]] size_t GetBlockCount() const;
    [[nodiscard]] size_t GetSegmentCount() const;
    [[nodiscard]] TString Print() const;

    // Returns the backend currently used for storage: Simple while the field
    // holds at most one range, the preferred backend after the switch.
    [[nodiscard]] EBackend GetBackend() const;

    // Memory pool accessors (for tests and diagnostics).
    [[nodiscard]] size_t GetUsedBytes() const;
    [[nodiscard]] size_t GetPoolSize() const;

private:
    friend class TBlockRangeFieldTestAccessor;

    void CollapseImpl();

    ui16 MaxBlockCount;
    EBackend PreferredBackend;

    TBlockRangeFieldSimple Simple;
    std::unique_ptr<TBlockRangePool> Pool;
    // Owns the underlying arena allocator used by backends that allocate
    // fixed-size chunks (currently TBlockRangeFieldSet).
    // Must be declared before Impl so that Impl is destroyed before
    // ArenaAllocator (Impl's backends hold raw pointers into it).
    std::unique_ptr<IArenaAllocator> ArenaAllocator;
    std::unique_ptr<IBlockRangeFieldImpl> Impl;
};

////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
