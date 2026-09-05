#include "arena_allocator_index_pool.h"

#include "arena_allocator.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/vector.h>

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

        TVector<ui16> indices;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            ui16 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            indices.push_back(index);
        }

        // All indices should be unique.
        std::unordered_set<ui16> unique(indices.begin(), indices.end());
        UNIT_ASSERT_VALUES_EQUAL(ChunksPerSlot, unique.size());

        // Only a single slot should have been acquired.
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        for (ui16 index: indices) {
            pool.Deallocate(index);
        }

        // Slots stay alive for the lifetime of the pool so that indices
        // remain stable. Memory is released only when the pool is destroyed.
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());
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

        ui16 first = pool.Allocate();
        ui16 second = pool.Allocate();

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

        ui16 index = pool.Allocate();
        UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
        pool.Deallocate(index);

        ui16 index2 = pool.Allocate();
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

        TVector<ui16> indices;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
        }

        // Snapshot addresses before deallocation.
        TVector<void*> addrs;
        for (ui16 index: indices) {
            addrs.push_back(pool.GetAddress<void>(index));
        }

        for (ui16 index: indices) {
            pool.Deallocate(index);
        }

        // The slot is still alive (so the indices remain valid).
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        // Indices must still resolve to the same chunk addresses.
        for (size_t i = 0; i < indices.size(); ++i) {
            UNIT_ASSERT_EQUAL(addrs[i], pool.GetAddress<void>(indices[i]));
        }
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

            TVector<ui16> indices;
            for (size_t i = 0; i < ChunksPerSlot; ++i) {
                indices.push_back(pool.Allocate());
            }
            for (ui16 index: indices) {
                pool.Deallocate(index);
            }

            // The slot is still alive while the pool exists.
            UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());
        }

        // All slot memory must have been returned to the allocator.
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

        TVector<ui16> indices;
        for (size_t i = 0; i < 2 * ChunksPerSlot; ++i) {
            indices.push_back(pool.Allocate());
        }
        UNIT_ASSERT_VALUES_EQUAL(2, rawAllocator->AllocatedBlocks());

        // Free chunks from the second slot only - both slots stay alive
        // because returning a slot would invalidate indices of later slots.
        for (size_t i = ChunksPerSlot; i < indices.size(); ++i) {
            pool.Deallocate(indices[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(2, rawAllocator->AllocatedBlocks());

        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            pool.Deallocate(indices[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(2, rawAllocator->AllocatedBlocks());
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

        TVector<ui16> indices;
        for (size_t i = 0; i < MaxChunks; ++i) {
            ui16 index = pool.Allocate();
            UNIT_ASSERT(TArenaAllocatorIndexPool::InvalidIndex != index);
            indices.push_back(index);
        }

        // The pool is exhausted now.
        UNIT_ASSERT_VALUES_EQUAL(
            TArenaAllocatorIndexPool::InvalidIndex,
            pool.Allocate());

        for (ui16 index: indices) {
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
        TVector<ui16> firstSlot;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            firstSlot.push_back(pool.Allocate());
            UNIT_ASSERT_VALUES_EQUAL(static_cast<ui16>(i), firstSlot.back());
        }

        // The next chunk should come from a new slot and have an index
        // of ChunksPerSlot.
        ui16 next = pool.Allocate();
        UNIT_ASSERT_VALUES_EQUAL(static_cast<ui16>(ChunksPerSlot), next);

        pool.Deallocate(next);
        for (ui16 index: firstSlot) {
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

        ui16 index = pool.Allocate();
        int* p = pool.GetAddress<int>(index);
        UNIT_ASSERT(p);
        *p = 42;

        const int* cp = pool.GetAddress<const int>(index);
        UNIT_ASSERT_EQUAL(42, *cp);

        pool.Deallocate(index);
    }
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
