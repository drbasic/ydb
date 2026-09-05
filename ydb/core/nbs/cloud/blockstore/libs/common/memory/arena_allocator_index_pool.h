#pragma once

#include "arena_allocator.h"

#include <ydb/core/nbs/cloud/storage/core/libs/common/disable_copy.h>

#include <util/generic/vector.h>

#include <cstddef>
#include <memory>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// A fixed-size-chunk pool that hands out ui16 indices instead of pointers.
// The index encodes the slot and the chunk within the slot, so stable chunk
// addresses are guaranteed by keeping slots in a vector of unique_ptrs.
class TArenaAllocatorIndexPool
{
public:
    static constexpr ui16 InvalidIndex = 0xFFFF;

    explicit TArenaAllocatorIndexPool(
        IArenaAllocatorPtr allocator,
        size_t slotSize,
        size_t maxSizeBytes,
        size_t chunkSize);
    ~TArenaAllocatorIndexPool();

    TArenaAllocatorIndexPool(const TArenaAllocatorIndexPool&) = delete;
    TArenaAllocatorIndexPool& operator=(
        const TArenaAllocatorIndexPool&) = delete;
    TArenaAllocatorIndexPool(TArenaAllocatorIndexPool&&) = default;
    TArenaAllocatorIndexPool& operator=(TArenaAllocatorIndexPool&&) = default;

    // Returns InvalidIndex if the pool is exhausted.
    [[nodiscard]] ui16 Allocate();
    void Deallocate(ui16 index) noexcept;

    template <typename T>
    [[nodiscard]] T* GetAddress(ui16 index) const noexcept
    {
        if (index == InvalidIndex) {
            return nullptr;
        }
        return static_cast<T*>(GetChunkAddress(index));
    }

    template <typename T>
    [[nodiscard]] T* GetAddress(ui16 index) noexcept
    {
        if (index == InvalidIndex) {
            return nullptr;
        }
        return static_cast<T*>(GetChunkAddress(index));
    }

private:
    // Intrusive free list node stored in the first bytes of a free chunk
    // (same trick as in arena_allocator_pool.cpp).
    struct TFreeChunk
    {
        TFreeChunk* Next = nullptr;
    };

    // A slot allocates its chunks itself, carving them sequentially from
    // the base pointer and keeping freed chunks in an intrusive free list.
    // The slot owns its memory and returns it to the allocator in its
    // destructor.
    struct TSlot
    {
        IArenaAllocator* Allocator;
        void* Base = nullptr;
        const size_t ChunksPerSlot = 0;
        const size_t ChunkSize = 0;
        size_t AllocatedChunks = 0;   // chunks carved so far
        TFreeChunk* FreeList = nullptr;
        size_t FreeCount = 0;

        TSlot(
            IArenaAllocator* allocator,
            size_t slotSize,
            size_t chunksPerSlot,
            size_t chunkSize);
        ~TSlot();

        void* Allocate();
        void Free(void* chunk) noexcept;
        [[nodiscard]] bool Full() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
    };

    void* GetChunkAddress(ui16 index) const noexcept;

    void AcquireSlot();
    void ReleaseSlot(size_t slotIndex) noexcept;

    IArenaAllocatorPtr Allocator;
    size_t SlotSize;
    size_t ChunkSize;
    size_t ChunksPerSlot;
    // Upper bound for the total number of chunks in the pool.
    size_t MaxChunks;

    TVector<std::unique_ptr<TSlot>> Slots;
    // Slot currently being carved into chunks.
    TSlot* CurrentSlot = nullptr;
    // Index of CurrentSlot within Slots.
    size_t CurrentSlotIndex = 0;
};

/////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
