#pragma once

#include "arena_allocator.h"

#include <ydb/core/nbs/cloud/storage/core/libs/common/disable_copy.h>

#include <util/generic/list.h>
#include <util/generic/map.h>

#include <cstddef>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

class TArenaAllocatorPool: TDisableCopy
{
public:
    explicit TArenaAllocatorPool(
        IArenaAllocatorPtr allocator,
        size_t slotSize = 0);
    ~TArenaAllocatorPool();

    void* Allocate(size_t size);
    void Deallocate(void*) noexcept;

private:
    // Intrusive free list node stored in the first bytes of a free chunk
    // (same trick as TBlock::TSlot in arena_allocator.cpp).
    struct TFreeChunk
    {
        TFreeChunk* Next = nullptr;
    };

    // A slot allocates its chunks itself, carving them sequentially from
    // the base pointer and keeping freed chunks in an intrusive free list
    // (same pattern as TBlock in arena_allocator.cpp). The slot owns its
    // memory and returns it to the allocator in its destructor.
    struct TSlot
    {
        void* Base = nullptr;
        IArenaAllocatorPtr Allocator;
        const size_t SlotSize = 0;
        const size_t ChunkSize = 0;
        const size_t MaxChunkCapacity = 0;
        size_t AllocatedChunks = 0;   // chunks carved so far
        TFreeChunk* FreeList = nullptr;
        size_t FreeCount = 0;

        TSlot(IArenaAllocatorPtr allocator, size_t slotSize, size_t chunkSize);
        ~TSlot();

        void* Allocate();
        void Free(void* chunk) noexcept;
        [[nodiscard]] bool Full() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
    };

    // A collection of slots for a single chunk size.
    class TSlots
    {
    public:
        TSlots() = default;
        TSlot* Acquire(
            IArenaAllocatorPtr allocator,
            size_t chunkSize,
            size_t slotSize);
        void Release(TSlot* slot) noexcept;

        [[nodiscard]] bool Empty() const noexcept
        {
            return Slots.empty();
        }

        TList<TSlot> Slots;
        TSlot* CurrentSlot = nullptr;

    private:
        size_t SlotSize = 0;
    };

    using TSizeMap = TMap<size_t, TSlots>;

    IArenaAllocatorPtr Allocator;
    const size_t SlotSize;
    TSizeMap SizeMap;
    // Slot bases for O(log n) lookup in Deallocate.
    TMap<void*, TSlot*> Bases;
};

/////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
