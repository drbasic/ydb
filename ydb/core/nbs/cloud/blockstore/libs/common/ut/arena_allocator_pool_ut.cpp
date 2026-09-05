#include "arena_allocator_pool.h"

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

Y_UNIT_TEST_SUITE(ArenaAllocatorPoolTest)
{
    Y_UNIT_TEST(AllocateAndFreeChunks)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorPool pool(rawAllocator, SlotSize, ChunkSize);

        TVector<void*> ptrs;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            void* ptr = pool.Allocate(ChunkSize);
            UNIT_ASSERT(ptr);
            ptrs.push_back(ptr);
        }

        // All pointers should be unique.
        std::unordered_set<void*> unique(ptrs.begin(), ptrs.end());
        UNIT_ASSERT_VALUES_EQUAL(ChunksPerSlot, unique.size());

        // Only a single slot should have been acquired.
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        for (void* ptr: ptrs) {
            pool.Deallocate(ptr);
        }

        // The whole slot should be returned to the allocator.
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }

    Y_UNIT_TEST(ChunkSizeIsRespected)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;

        TArenaAllocatorPool pool(CreateArenaAllocator(), SlotSize, ChunkSize);

        void* first = pool.Allocate(ChunkSize);
        void* second = pool.Allocate(ChunkSize);

        const auto diff =
            static_cast<char*>(second) - static_cast<char*>(first);
        UNIT_ASSERT_VALUES_EQUAL(ChunkSize, diff);

        pool.Deallocate(first);
        pool.Deallocate(second);
    }

    Y_UNIT_TEST(FreedChunkIsReused)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;

        TArenaAllocatorPool pool(CreateArenaAllocator(), SlotSize, ChunkSize);

        void* ptr = pool.Allocate(ChunkSize);
        UNIT_ASSERT(ptr);
        pool.Deallocate(ptr);

        void* ptr2 = pool.Allocate(ChunkSize);
        UNIT_ASSERT_EQUAL(ptr, ptr2);

        pool.Deallocate(ptr2);
    }

    Y_UNIT_TEST(SlotIsReusedAfterFullRelease)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorPool pool(rawAllocator, SlotSize, ChunkSize);

        TVector<void*> ptrs;
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            ptrs.push_back(pool.Allocate(ChunkSize));
        }
        const void* slotBase = ptrs[0];

        for (void* ptr: ptrs) {
            pool.Deallocate(ptr);
        }
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());

        // The next allocation should re-acquire a slot and hand out
        // the same address again.
        void* ptr = pool.Allocate(ChunkSize);
        UNIT_ASSERT_EQUAL(slotBase, ptr);
        pool.Deallocate(ptr);
    }

    Y_UNIT_TEST(MultipleSlots)
    {
        constexpr size_t SlotSize = 4096;
        constexpr size_t ChunkSize = 512;
        constexpr size_t ChunksPerSlot = SlotSize / ChunkSize;

        auto allocator = std::make_unique<TTrackingAllocator>();
        auto* rawAllocator = allocator.get();
        TArenaAllocatorPool pool(rawAllocator, SlotSize, ChunkSize);

        TVector<void*> ptrs;
        for (size_t i = 0; i < 2 * ChunksPerSlot; ++i) {
            ptrs.push_back(pool.Allocate(ChunkSize));
        }
        UNIT_ASSERT_VALUES_EQUAL(2, rawAllocator->AllocatedBlocks());

        // Free chunks from the second slot only - its slot should be
        // returned to the allocator while the first one stays alive.
        for (size_t i = ChunksPerSlot; i < ptrs.size(); ++i) {
            pool.Deallocate(ptrs[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(1, rawAllocator->AllocatedBlocks());

        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            pool.Deallocate(ptrs[i]);
        }
        UNIT_ASSERT_VALUES_EQUAL(0, rawAllocator->AllocatedBlocks());
    }
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
