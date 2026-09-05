#include "arena_allocator.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/size_literals.h>
#include <util/generic/vector.h>
#include <util/system/thread.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <thread>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(ArenaAllocatorTest)
{
    Y_UNIT_TEST(AllocateAllSizes)
    {
        std::unique_ptr<IArenaAllocator> allocator(CreateArenaAllocator());

        for (size_t size:
             {size_t(512), size_t(1024), size_t(2048), size_t(4096)})
        {
            void* ptr = allocator->Allocate(size);
            UNIT_ASSERT(ptr);
            UNIT_ASSERT_VALUES_EQUAL(size, allocator->AllocatedSize());
            allocator->DeAllocate(ptr);
            UNIT_ASSERT_VALUES_EQUAL(0, allocator->AllocatedSize());
        }
    }

    Y_UNIT_TEST(AllocationIsAligned)
    {
        std::unique_ptr<IArenaAllocator> allocator(CreateArenaAllocator());

        for (size_t size:
             {size_t(512), size_t(1024), size_t(2048), size_t(4096)})
        {
            void* ptr = allocator->Allocate(size);
            UNIT_ASSERT(ptr);
            UNIT_ASSERT_VALUES_EQUAL(0, reinterpret_cast<uintptr_t>(ptr) % 16);
            allocator->DeAllocate(ptr);
        }
    }

    Y_UNIT_TEST(ReuseFreedSlot)
    {
        std::unique_ptr<IArenaAllocator> allocator(CreateArenaAllocator());

        void* ptr = allocator->Allocate(1024);
        UNIT_ASSERT(ptr);
        allocator->DeAllocate(ptr);

        void* ptr2 = allocator->Allocate(1024);
        UNIT_ASSERT_EQUAL(ptr, ptr2);

        allocator->DeAllocate(ptr2);
    }

    Y_UNIT_TEST(BlocksAreReused)
    {
        std::unique_ptr<IArenaAllocator> allocator(CreateArenaAllocator());

        TVector<void*> ptrs;
        for (size_t i = 0; i < 100; ++i) {
            ptrs.push_back(allocator->Allocate(512));
        }
        UNIT_ASSERT_VALUES_EQUAL(100, allocator->AllocatedBlocks());
        UNIT_ASSERT_VALUES_EQUAL(100 * 512, allocator->AllocatedSize());

        // All pointers should be unique.
        std::unordered_set<void*> unique(ptrs.begin(), ptrs.end());
        UNIT_ASSERT_VALUES_EQUAL(100, unique.size());

        for (void* ptr: ptrs) {
            allocator->DeAllocate(ptr);
        }
        UNIT_ASSERT_VALUES_EQUAL(0, allocator->AllocatedBlocks());
        UNIT_ASSERT_VALUES_EQUAL(0, allocator->AllocatedSize());

        // After freeing everything the arena should hand out the same
        // addresses again (the block was returned and re-acquired).
        void* ptr = allocator->Allocate(512);
        UNIT_ASSERT(ptr);
        UNIT_ASSERT_VALUES_EQUAL(512, allocator->AllocatedSize());
        allocator->DeAllocate(ptr);
    }

    Y_UNIT_TEST(Multithreaded)
    {
        std::unique_ptr<IArenaAllocator> allocator(CreateArenaAllocator());

        constexpr int ThreadCount = 8;
        constexpr int Iterations = 10000;

        std::atomic<int> failed{0};

        TVector<std::thread> threads;
        for (int t = 0; t < ThreadCount; ++t) {
            threads.emplace_back(
                [&, t]
                {
                    try {
                        for (int i = 0; i < Iterations; ++i) {
                            const size_t size = size_t(512) << ((t + i) % 4);
                            void* ptr = allocator->Allocate(size);
                            if (!ptr) {
                                failed++;
                                return;
                            }
                            allocator->DeAllocate(ptr);
                        }
                    } catch (...) {
                        failed++;
                    }
                });
        }

        for (auto& thread: threads) {
            thread.join();
        }

        UNIT_ASSERT_VALUES_EQUAL(0, failed.load());
        UNIT_ASSERT_VALUES_EQUAL(0, allocator->AllocatedBlocks());
        UNIT_ASSERT_VALUES_EQUAL(0, allocator->AllocatedSize());
    }
}

//////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
