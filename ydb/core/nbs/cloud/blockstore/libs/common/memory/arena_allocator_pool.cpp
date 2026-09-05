#include "arena_allocator_pool.h"

#include <util/generic/algorithm.h>
#include <util/system/yassert.h>

#include <cstring>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

TArenaAllocatorPool::TSlot::TSlot(
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

TArenaAllocatorPool::TSlot::~TSlot()
{
    if (Base) {
        Allocator->DeAllocate(Base);
    }
}

void* TArenaAllocatorPool::TSlot::Allocate()
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
    return static_cast<char*>(Base) + AllocatedChunks++ * ChunkSize;
}

void TArenaAllocatorPool::TSlot::Free(void* chunk) noexcept
{
    auto* ptr = static_cast<TFreeChunk*>(chunk);
    ptr->Next = FreeList;
    FreeList = ptr;
    ++FreeCount;
}

bool TArenaAllocatorPool::TSlot::Full() const noexcept
{
    return AllocatedChunks == ChunksPerSlot && FreeCount == 0;
}

bool TArenaAllocatorPool::TSlot::Empty() const noexcept
{
    return FreeCount == AllocatedChunks;
}

//////////////////////////////////////////////////////////////////////////////

TArenaAllocatorPool::TArenaAllocatorPool(
    IArenaAllocatorPtr allocator,
    size_t slotSize,
    size_t chunkSize)
    : Allocator(allocator)
    , SlotSize(slotSize)
    , ChunkSize(chunkSize)
    , ChunksPerSlot(SlotSize / ChunkSize)
{
    Y_ABORT_UNLESS(Allocator);
    Y_ABORT_UNLESS(SlotSize);
    Y_ABORT_UNLESS(ChunkSize);
    Y_ABORT_UNLESS(ChunkSize <= SlotSize);
    Y_ABORT_UNLESS(SlotSize % ChunkSize == 0);
}

TArenaAllocatorPool::~TArenaAllocatorPool() = default;

void* TArenaAllocatorPool::Allocate(size_t size)
{
    Y_ABORT_UNLESS(size <= ChunkSize);

    if (!CurrentSlot) {
        AcquireSlot();
    }

    if (void* ptr = CurrentSlot->Allocate()) {
        return ptr;
    }

    AcquireSlot();
    return CurrentSlot->Allocate();
}

void TArenaAllocatorPool::Deallocate(void* ptr) noexcept
{
    if (!ptr) {
        return;
    }

    // Find the slot whose [Base, Base + SlotSize) range contains ptr:
    // it is the slot with the greatest base <= ptr.
    auto it = Bases.upper_bound(ptr);
    if (it == Bases.begin()) {
        // Unknown pointer.
        Y_ABORT_UNLESS(false, "Deallocate: unknown pointer");
    }
    --it;
    auto* slot = it->second;
    Y_ABORT_UNLESS(
        static_cast<char*>(ptr) < static_cast<char*>(slot->Base) + SlotSize,
        "Deallocate: unknown pointer");

    slot->Free(ptr);

    if (slot->Empty()) {
        // The whole slot is free again - destroy it, returning its memory
        // to the allocator.
        ReleaseSlot(slot);
    }
}

void TArenaAllocatorPool::AcquireSlot()
{
    // Try to find an existing slot with free chunks.
    for (auto& slot: Slots) {
        if (!slot.Full()) {
            CurrentSlot = &slot;
            return;
        }
    }

    // Allocate a new slot.
    Slots.emplace_back(Allocator, SlotSize, ChunksPerSlot, ChunkSize);
    CurrentSlot = &Slots.back();
    Bases.emplace(CurrentSlot->Base, CurrentSlot);
}

void TArenaAllocatorPool::ReleaseSlot(TSlot* slot) noexcept
{
    if (CurrentSlot == slot) {
        CurrentSlot = nullptr;
    }
    Bases.erase(slot->Base);
    Slots.remove_if([slot](const TSlot& s) { return &s == slot; });
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
