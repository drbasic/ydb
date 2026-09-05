#include "arena_allocator_index_pool.h"

#include <util/system/yassert.h>

#include <cstring>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

TArenaAllocatorIndexPool::TSlot::TSlot(
    IArenaAllocator* allocator,
    size_t slotSize,
    size_t chunksPerSlot,
    size_t chunkSize)
    : Allocator(allocator)
    , Base(allocator->Allocate(slotSize))
    , ChunksPerSlot(chunksPerSlot)
    , ChunkSize(chunkSize)
{
    Y_ABORT_UNLESS(Base);
}

TArenaAllocatorIndexPool::TSlot::~TSlot()
{
    if (Base) {
        Allocator->DeAllocate(Base);
    }
}

void* TArenaAllocatorIndexPool::TSlot::Allocate()
{
    if (FreeList) {
        TFreeChunk* result = FreeList;
        FreeList = FreeList->Next;
        std::memset(result, 0, ChunkSize);
        --FreeCount;
        return result;
    }
    if (AllocatedChunks == ChunksPerSlot) {
        return nullptr;
    }
    return static_cast<char*>(Base) + (AllocatedChunks++ * ChunkSize);
}

void TArenaAllocatorIndexPool::TSlot::Free(void* chunk) noexcept
{
    auto* ptr = static_cast<TFreeChunk*>(chunk);
    ptr->Next = FreeList;
    FreeList = ptr;
    ++FreeCount;
}

bool TArenaAllocatorIndexPool::TSlot::Full() const noexcept
{
    return AllocatedChunks == ChunksPerSlot && FreeCount == 0;
}

bool TArenaAllocatorIndexPool::TSlot::Empty() const noexcept
{
    return FreeCount == AllocatedChunks;
}

//////////////////////////////////////////////////////////////////////////////

TArenaAllocatorIndexPool::TArenaAllocatorIndexPool(
    IArenaAllocatorPtr allocator,
    size_t slotSize,
    size_t maxSizeBytes,
    size_t chunkSize)
    : Allocator(allocator)
    , SlotSize(slotSize)
    , ChunkSize(chunkSize)
    , ChunksPerSlot(SlotSize / ChunkSize)
    , MaxChunks(maxSizeBytes / ChunkSize)
{
    Y_ABORT_UNLESS(Allocator);
    Y_ABORT_UNLESS(SlotSize);
    Y_ABORT_UNLESS(ChunkSize);
    Y_ABORT_UNLESS(ChunkSize <= SlotSize);
    Y_ABORT_UNLESS(SlotSize % ChunkSize == 0);
    // ui16 index: slotIndex * ChunksPerSlot + chunkIndex must fit.
    Y_ABORT_UNLESS(MaxChunks <= 65534);
}

TArenaAllocatorIndexPool::~TArenaAllocatorIndexPool() = default;

ui16 TArenaAllocatorIndexPool::Allocate()
{
    if (!CurrentSlot) {
        AcquireSlot();
    }

    if (void* ptr = CurrentSlot->Allocate()) {
        const size_t chunkIndex =
            static_cast<char*>(ptr) - static_cast<char*>(CurrentSlot->Base);
        Y_ABORT_UNLESS(chunkIndex % ChunkSize == 0);
        return static_cast<ui16>(
            (CurrentSlotIndex * ChunksPerSlot) + (chunkIndex / ChunkSize));
    }

    if ((Slots.size() + 1) * ChunksPerSlot > MaxChunks) {
        // The pool is exhausted.
        return InvalidIndex;
    }

    AcquireSlot();
    void* ptr = CurrentSlot->Allocate();
    Y_ABORT_UNLESS(ptr);
    const size_t chunkIndex =
        static_cast<char*>(ptr) - static_cast<char*>(CurrentSlot->Base);
    Y_ABORT_UNLESS(chunkIndex % ChunkSize == 0);
    return static_cast<ui16>(
        (CurrentSlotIndex * ChunksPerSlot) + (chunkIndex / ChunkSize));
}

void TArenaAllocatorIndexPool::Deallocate(ui16 index) noexcept
{
    if (index == InvalidIndex) {
        return;
    }

    const size_t slotIndex = index / ChunksPerSlot;
    const size_t chunkIndex = index % ChunksPerSlot;

    Y_ABORT_UNLESS(slotIndex < Slots.size(), "Deallocate: unknown index");

    auto* slot = Slots[slotIndex].get();
    slot->Free(static_cast<char*>(slot->Base) + (chunkIndex * ChunkSize));

    // Note: slots are not returned to the underlying allocator here, because
    // that would invalidate ui16 indices that point into later slots.
    // Memory is released when the pool is destroyed.
}

void* TArenaAllocatorIndexPool::GetChunkAddress(ui16 index) const noexcept
{
    const size_t slotIndex = index / ChunksPerSlot;
    const size_t chunkIndex = index % ChunksPerSlot;

    Y_ABORT_UNLESS(slotIndex < Slots.size(), "GetAddress: unknown index");

    auto* slot = Slots[slotIndex].get();
    return static_cast<char*>(slot->Base) + (chunkIndex * ChunkSize);
}

void TArenaAllocatorIndexPool::AcquireSlot()
{
    // Try to find an existing slot with free chunks.
    for (size_t i = 0; i < Slots.size(); ++i) {
        if (!Slots[i]->Full()) {
            CurrentSlot = Slots[i].get();
            CurrentSlotIndex = i;
            return;
        }
    }

    // Allocate a new slot.
    Slots.push_back(
        std::make_unique<TSlot>(Allocator, SlotSize, ChunksPerSlot, ChunkSize));
    CurrentSlot = Slots.back().get();
    CurrentSlotIndex = Slots.size() - 1;
}

void TArenaAllocatorIndexPool::ReleaseSlot(size_t /*slotIndex*/) noexcept
{
    // Slots are kept alive for the lifetime of the pool so that ui16 indices
    // remain stable. Memory is returned to the underlying allocator when the
    // pool is destroyed.
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
