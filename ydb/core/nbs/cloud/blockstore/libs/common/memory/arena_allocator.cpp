#include "arena_allocator.h"

#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/generic/list.h>
#include <util/generic/map.h>
#include <util/generic/size_literals.h>
#include <util/generic/vector.h>
#include <util/system/mutex.h>
#include <util/system/yassert.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace NYdb::NBS::NBlockStore {

namespace {

//////////////////////////////////////////////////////////////////////////////

constexpr size_t BlockSize = 1_MB;
constexpr size_t MinSlotSize = 256;
constexpr size_t MaxSlotSize = 4096;
constexpr size_t SlotSizeCount = 5;   // 256, 512, 1024, 2048, 4096

//////////////////////////////////////////////////////////////////////////////

class TBlock;
using TBase = void*;
using TBlockPtr = TBlock*;
using TBlocks = TMap<void*, TBlock*>;

//////////////////////////////////////////////////////////////////////////////

void* AlignedAlloc(size_t size, size_t alignment)
{
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void AlignedFree(void* ptr)
{
    free(ptr);
}

size_t SlotIndexForSize(size_t size)
{
    // Only exact power-of-two slot sizes within [MinSlotSize, MaxSlotSize]
    // are allowed.
    Y_ABORT_UNLESS(
        size >= MinSlotSize && size <= MaxSlotSize && (size & (size - 1)) == 0);
    return size == MinSlotSize
               ? 0
               : static_cast<size_t>(__builtin_ctzll(size)) -
                     static_cast<size_t>(__builtin_ctzll(MinSlotSize));
}

////////////////////////////////////////////////////////////////////////////////

class TBlock
{
    struct TSlot
    {
        TSlot* Next = nullptr;
    };

public:
    explicit TBlock(size_t slotSize)
        : Base(AlignedAlloc(BlockSize, 8))
        , SlotSize(slotSize)
        , SlotsPerBlock(BlockSize / slotSize)
    {
        std::memset(Base, 0, BlockSize);
    }

    ~TBlock()
    {
        AlignedFree(Base);
    }

    TBase GetBase() const
    {
        return Base;
    }

    size_t GetSlotSize() const
    {
        return SlotSize;
    }

    void* Allocate()
    {
        if (FreeList) {
            TSlot* result = FreeList;
            FreeList = FreeList->Next;
            std::memset(result, 0, SlotSize);
            --FreeCount;
            return result;
        }
        if (AllocatedSlots == SlotsPerBlock) {
            return nullptr;
        }
        return static_cast<char*>(Base) + AllocatedSlots++ * SlotSize;
    }

    void Free(void* slot)
    {
        TSlot* ptr = static_cast<TSlot*>(slot);
        ptr->Next = FreeList;
        FreeList = ptr;
        ++FreeCount;
    }

    bool Empty() const
    {
        return FreeCount == AllocatedSlots;
    }

    [[nodiscard]] size_t GetAllocatedSlotsSize() const
    {
        return (AllocatedSlots - FreeCount) * SlotSize;
    }

private:
    const TBase Base = nullptr;
    const size_t SlotSize = 0;
    const size_t SlotsPerBlock = 0;
    size_t AllocatedSlots = 0;
    TSlot* FreeList = nullptr;
    size_t FreeCount = 0;
};

class TBlockList
{
public:
    explicit TBlockList(size_t slotSize)
        : SlotSize(slotSize)
    {}

    void* Allocate(TBlockPtr* block)
    {
        if (LastUsed) {
            if (auto* result = LastUsed->Allocate()) {
                return result;
            }
        }

        for (auto& block: Blocks) {
            if (auto* result = block.Allocate()) {
                LastUsed = &block;
                return result;
            }
        }

        Blocks.emplace_back(SlotSize);
        LastUsed = &Blocks.back();
        *block = LastUsed;
        return LastUsed->Allocate();
    }

    void FreeBlock(TBlockPtr block)
    {
        if (block == LastUsed) {
            LastUsed = nullptr;
        }
        for (auto it = Blocks.begin(); it != Blocks.end(); ++it) {
            if (&*it == block) {
                Blocks.erase(it);
                return;
            }
        }
        Y_ABORT_UNLESS(false, "FreeBlock: unknown block");
    }

    [[nodiscard]] size_t GetAllocatedSize() const
    {
        return Accumulate(
            Blocks,
            0,
            [](size_t result, const TBlock& block)
            { return result + block.GetAllocatedSlotsSize(); });
    }

private:
    const size_t SlotSize = 0;
    TList<TBlock> Blocks;
    TBlock* LastUsed = nullptr;
};

}   // namespace

//////////////////////////////////////////////////////////////////////////////

class TArenaAllocator final: public IArenaAllocator
{
public:
    void* Allocate(size_t size) override
    {
        with_lock (Mutex) {
            ++AllocatedBlockCount;
            TBlockPtr newBlock = nullptr;
            void* result = GetBlockList(size).Allocate(&newBlock);
            if (newBlock) {
                Bases.emplace(newBlock->GetBase(), newBlock);
            }
            return result;
        }
    }

    void DeAllocate(void* ptr) override
    {
        if (!ptr) {
            return;
        }

        with_lock (Mutex) {
            --AllocatedBlockCount;
            // Find the block whose [Base, Base + BlockSize) range contains
            // ptr: it is the block with the greatest base <= ptr.
            auto it = Bases.upper_bound(ptr);
            if (it == Bases.begin()) {
                // Unknown pointer.
                Y_ABORT_UNLESS(false, "DeAllocate: unknown pointer");
            }
            --it;
            TBlock* block = it->second;
            Y_ABORT_UNLESS(
                static_cast<char*>(ptr) <
                    static_cast<char*>(block->GetBase()) + BlockSize,
                "DeAllocate: unknown pointer");
            block->Free(ptr);
            if (block->Empty()) {
                Bases.erase(it);
                GetBlockList(block->GetSlotSize()).FreeBlock(block);
            }
        }
    }

    [[nodiscard]] size_t AllocatedBlocks() const override
    {
        with_lock (Mutex) {
            return AllocatedBlockCount;
        }
    }

    [[nodiscard]] size_t AllocatedSize() const override
    {
        with_lock (Mutex) {
            return Accumulate(
                BlocksBySlotSize,
                0,
                [](size_t result, const TBlockList& block)
                { return result + block.GetAllocatedSize(); });
        }
    }

private:
    TBlockList& GetBlockList(size_t slotSize)
    {
        return BlocksBySlotSize[SlotIndexForSize(slotSize)];
    }

    TMutex Mutex;
    TMap<TBase, TBlock*> Bases;
    std::array<TBlockList, SlotSizeCount> BlocksBySlotSize{
        TBlockList(256),
        TBlockList(512),
        TBlockList(1024),
        TBlockList(2048),
        TBlockList(4096)};
    size_t AllocatedBlockCount = 0;
};

//////////////////////////////////////////////////////////////////////////////

IArenaAllocator* CreateArenaAllocator()
{
    return new TArenaAllocator();
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
