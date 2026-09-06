#include "arena_allocator_index_pool.h"

#include "arena_allocator.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/vector.h>

#include <cstring>
#include <memory>
#include <unordered_set>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

namespace {

struct TTrackingAllocator final: public IArenaAllocator
{
    size_t AllocatedCount = 0;

    void* Allocate(size_t size) override
    {
        ++AllocatedCount;
        return ::operator new(size);
    }

    void DeAllocate(void* ptr) override
    {
        --AllocatedCount;
        ::operator delete(ptr);
    }

    size_t AllocatedBlocks() const override
    {
        return AllocatedCount;
    }

    size_t AllocatedSize() const override
    {
        return AllocatedCount;
    }
};

}   // namespace

Y_UNIT_TEST_SUITE(ArenaAllocatorIndexPoolTest)
{
    Y_UNIT_TEST(AllocateAndFreeChunks)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorIndexPool pool(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        TVector<ui64> indices;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            const ui64 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            indices.push_back(index);
        }

        // All indices should be unique.
        std::unordered_set<ui64> unique(indices.begin(), indices.end());
        UNIT_ASSERT_VALUES_EQUAL(ChunksPerSlot, unique.size());

        // Only a single slot should have been acquired.
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        for (ui64 index: indices) {
            pool.Deallocate(index);
        }

        // The slot is released when all chunks are freed.
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(ChunkSizeIsRespected)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        ui64 first = pool.Allocate();
        ui64 second = pool.Allocate();

        const auto diff = static_cast<char*>(pool.GetAddress<void>(second)) -
                          static_cast<char*>(pool.GetAddress<void>(first));
        UNIT_ASSERT_VALUES_EQUAL(ChunkSize, diff);

        pool.Deallocate(first);
        pool.Deallocate(second);
    }

    Y_UNIT_TEST(FreedChunkIsReused)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        ui64 index = pool.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
        pool.Deallocate(index);

        ui64 index2 = pool.Allocate();
        UNIT_ASSERT_EQUAL(index, index2);

        pool.Deallocate(index2);
    }

    Y_UNIT_TEST(IndicesRemainValidAfterDeallocation)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorIndexPool pool(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        TVector<ui64> indices;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
        }

        // Snapshot addresses before deallocation.
        TVector<void*> addrs;
        for (ui64 index: indices) {
            addrs.push_back(pool.GetAddress<void>(index));
        }

        for (ui64 index: indices) {
            pool.Deallocate(index);
        }

        // The slot is released.
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(MemoryReleasedOnDestruction)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();

        {
            TArenaAllocatorIndexPool pool(
                rawAllocator,
                SlotSize,
                MaxSizeBytes,
                ChunkSize);

            TVector<ui64> indices;
            for (size_t i = 0; i < ChunksPerSlot; ++i) {
                indices.push_back(pool.Allocate());
            }
            for (ui64 index: indices) {
                pool.Deallocate(index);
            }

            // The slot has been released because all chunks were freed.
            UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
        }

        // Memory must have been returned to the allocator when slots were
        // reclaimed.
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(MultipleSlots)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 4 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorIndexPool pool(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        TVector<ui64> indices;
        for (size_t i = 0; i < 2 * ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
        }
        UNIT_ASSERT_VALUES_EQUAL(2, rawAllocator->AllocatedBlocks());

        // Free chunks from the second slot - it gets released.
        for (size_t i = ChunksPerSlot; i < indices.size(); ++i) {
            pool.Deallocate(indices[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            pool.Deallocate(indices[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(GetAddressForInvalidIndexReturnsNullptr)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        UNIT_ASSERT(
            nullptr ==
            pool.GetAddress<int>(TArenaAllocatorIndexPool::InvalidIndex));
        UNIT_ASSERT(
            nullptr ==
            pool.GetAddress<const int>(TArenaAllocatorIndexPool::InvalidIndex));

        pool.Deallocate(TArenaAllocatorIndexPool::InvalidIndex);
    }

    Y_UNIT_TEST(PoolExhaustionReturnsInvalidIndex)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxChunks = 2 * ChunksPerSlot;
        constexpr size_t MaxSizeBytes = MaxChunks * ChunkSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        TVector<ui64> indices;
        for (size_t i = 0; i < MaxChunks; ++i) {
            ui64 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            indices.push_back(index);
        }

        // The pool is exhausted now.
        UNIT_ASSERT_VALUES_EQUAL(
            TArenaAllocatorIndexPool::InvalidIndex,
            pool.Allocate());

        for (ui64 index: indices) {
            pool.Deallocate(index);
        }
    }

    Y_UNIT_TEST(IndexEncodingIsStable)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 4 * SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        // Allocate all chunks from the first slot - their indices must be
        // exactly 0..ChunksPerSlot-1.
        TVector<ui64> firstSlot;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            firstSlot.push_back(pool.Allocate());
            UNIT_ASSERT_VALUES_EQUAL(static_cast<ui64>(i), firstSlot.back());
        }

        // The next chunk should come from a new slot and have an index
        // of ChunksPerSlot.
        ui64 next = pool.Allocate();
        UNIT_ASSERT_VALUES_EQUAL(static_cast<ui64>(ChunksPerSlot), next);

        pool.Deallocate(next);
        for (ui64 index: firstSlot) {
            pool.Deallocate(index);
        }
    }

    Y_UNIT_TEST(GetAddressReturnsTypedPointer)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        ui64 index = pool.Allocate();
        int* p = pool.GetAddress<int>(index);
        UNIT_ASSERT(p);
        *p = 42;

        const int* cp = pool.GetAddress<const int>(index);
        UNIT_ASSERT_EQUAL(42, *cp);

        pool.Deallocate(index);
    }

    Y_UNIT_TEST(MoveSemanticsPreservesIndicesAndData)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorIndexPool pool(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        // Allocate several chunks and write unique values into them.
        constexpr size_t Allocated = 4;
        TVector<ui64> indices;
        TVector<int> expected;
        for (size_t i = 0; i < Allocated; ++i) {
            ui64 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            int* p = pool.GetAddress<int>(index);
            UNIT_ASSERT(p);
            *p = static_cast<int>(i * 10 + 7);
            indices.push_back(index);
            expected.push_back(*p);
        }

        // Snapshot addresses before the move.
        TVector<void*> addrs;
        for (ui64 index: indices) {
            addrs.push_back(pool.GetAddress<void>(index));
        }

        // Move-construct a new pool from the original one.
        TArenaAllocatorIndexPool moved(std::move(pool));

        // The original pool must be empty after the move.
        UNIT_ASSERT(
            pool.GetAddress<int>(TArenaAllocatorIndexPool::InvalidIndex) ==
            nullptr);

        // Indices must remain valid and resolve to the same chunk addresses.
        for (size_t i = 0; i < indices.size(); ++i) {
            void* p = moved.GetAddress<void>(indices[i]);
            UNIT_ASSERT_EQUAL(addrs[i], p);
            int* ip = moved.GetAddress<int>(indices[i]);
            UNIT_ASSERT_EQUAL(expected[i], *ip);
        }

        // The moved pool must still be able to allocate and free chunks.
        ui64 extra = moved.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != extra);
        moved.Deallocate(extra);

        for (size_t i = 0; i < indices.size(); ++i) {
            moved.Deallocate(indices[i]);
        }

        // Memory must have been released when the last slots were freed.
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(MoveAssignmentPreservesIndicesAndData)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();

        TArenaAllocatorIndexPool source(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        ui64 index = source.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
        int* p = source.GetAddress<int>(index);
        UNIT_ASSERT(p);
        *p = 12345;

        void* addrBefore = source.GetAddress<void>(index);

        TArenaAllocatorIndexPool target(
            rawAllocator,
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        target = std::move(source);

        // The source must be empty after the move-assignment.
        UNIT_ASSERT(
            source.GetAddress<int>(TArenaAllocatorIndexPool::InvalidIndex) ==
            nullptr);

        // The index from the source pool must resolve in the target pool
        // and contain the same value.
        void* addrAfter = target.GetAddress<void>(index);
        UNIT_ASSERT_EQUAL(addrBefore, addrAfter);
        UNIT_ASSERT_EQUAL(12345, *target.GetAddress<int>(index));

        target.Deallocate(index);
    }

    Y_UNIT_TEST(FreedMemoryIsZeroedOnReuse)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t MaxSizeBytes = SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        // Allocate, write pattern, free.
        ui64 index1 = pool.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index1);
        int* ptr1 = pool.GetAddress<int>(index1);
        UNIT_ASSERT(ptr1);
        // Write a non-zero byte pattern through the chunk memory.
        void* chunkAddr = pool.GetAddress<void>(index1);
        std::memset(chunkAddr, 0xFF, ChunkSize);
        pool.Deallocate(index1);

        // Re-allocate and check zeroed.
        ui64 index2 = pool.Allocate();
        UNIT_ASSERT_EQUAL(index1, index2);

        char* data = static_cast<char*>(pool.GetAddress<void>(index2));
        for (size_t i = 0; i < ChunkSize; ++i) {
            UNIT_ASSERT_VALUES_EQUAL(0, static_cast<unsigned char>(data[i]));
        }

        pool.Deallocate(index2);
    }

    Y_UNIT_TEST(MultipleFreeAllocCyclesZeroed)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 1024;
        constexpr size_t MaxSizeBytes = SlotSize;
        constexpr int Cycles = 5;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        for (int cycle = 0; cycle < Cycles; ++cycle) {
            ui64 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            void* chunkAddr = pool.GetAddress<void>(index);
            UNIT_ASSERT(chunkAddr);

            if (cycle > 0) {
                char* data = static_cast<char*>(chunkAddr);
                for (size_t i = 0; i < ChunkSize; ++i) {
                    UNIT_ASSERT_VALUES_EQUAL(
                        0,
                        static_cast<unsigned char>(data[i]));
                }
            }

            std::memset(chunkAddr, 0xAA, ChunkSize);
            pool.Deallocate(index);
        }
    }

    Y_UNIT_TEST(ZeroedAfterSlotReclaimAndReallocate)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 2 * SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        TVector<ui64> indices;
        indices.reserve(ChunksPerSlot);
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
            void* chunkAddr = pool.GetAddress<void>(indices.back());
            std::memset(chunkAddr, 0xFF, ChunkSize);
        }

        // Free all chunks — slot memory is released.
        for (ui64 index: indices) {
            pool.Deallocate(index);
        }

        // Re-allocate and check zeroed.
        ui64 index = pool.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
        char* data = static_cast<char*>(pool.GetAddress<void>(index));
        for (size_t i = 0; i < ChunkSize; ++i) {
            UNIT_ASSERT_VALUES_EQUAL(0, static_cast<unsigned char>(data[i]));
        }

        pool.Deallocate(index);
    }

    Y_UNIT_TEST(ZeroedAcrossMultipleSlots)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;
        constexpr size_t MaxSizeBytes = 4 * SlotSize;

        TArenaAllocatorIndexPool pool(
            CreateArenaAllocator(),
            SlotSize,
            MaxSizeBytes,
            ChunkSize);

        // Allocate from two slots and write patterns.
        TVector<ui64> indices;
        for (size_t i = 0; i < 2 * ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
            void* chunkAddr = pool.GetAddress<void>(indices.back());
            std::memset(chunkAddr, 0xBB, ChunkSize);
        }

        // Free all chunks from the first slot.
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            pool.Deallocate(indices[i]);
        }

        // Allocate in the first slot — should be zeroed.
        ui64 index = pool.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
        char* data = static_cast<char*>(pool.GetAddress<void>(index));
        for (size_t i = 0; i < ChunkSize; ++i) {
            UNIT_ASSERT_VALUES_EQUAL(0, static_cast<unsigned char>(data[i]));
        }

        pool.Deallocate(index);
    }
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
