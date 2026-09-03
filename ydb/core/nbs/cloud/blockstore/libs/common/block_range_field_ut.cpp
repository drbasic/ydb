#include "block_range_field.h"

#include "block_range_allocator.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/vector.h>

namespace NYdb::NBS::NBlockStore {

namespace {

////////////////////////////////////////////////////////////////////////////////

TVector<TBlockRange64> Collect(const TBlockRangeField& field)
{
    TVector<TBlockRange64> result;
    field.Enumerate(
        [&](TBlockRange64 r)
        {
            result.push_back(r);
            return TBlockRangeField::EEnumerateContinuation::Continue;
        });
    return result;
}

TBlockRange64 R(ui64 start, ui64 end)
{
    return TBlockRange64::MakeClosedInterval(start, end);
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TBlockRangeFieldTest)
{
    // -------------------------------------------------------------------------
    // Memory measurement tests

    Y_UNIT_TEST(MemoryPerSingleRange)
    {
        // Measure memory used by a single added range
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(100, 200)));

        UNIT_ASSERT_GT(f.GetUsedBytes(), 0u);
        UNIT_ASSERT_LE(f.GetUsedBytes(), f.GetPoolSize());

        Cout << "Bytes per one range in TSet: " << f.GetUsedBytes() << Endl;
    }

    Y_UNIT_TEST(MemoryScalingWithRangeCount)
    {
        TBlockRangeField f(64 * 1024);
        for (ui64 i = 0; i < 1000; ++i) {
            UNIT_ASSERT(f.Add(R(i * 10, i * 10 + 5)));
        }

        UNIT_ASSERT_GT(f.GetUsedBytes(), 0u);
    }

    Y_UNIT_TEST(MemoryPerDisjointRange)
    {
        TBlockRangeField f(64 * 1024);
        // Add ranges with gaps so they don't merge
        for (ui64 i = 0; i < 100; ++i) {
            UNIT_ASSERT(f.Add(R(i * 20, i * 20 + 5)));
        }

        UNIT_ASSERT_GT(f.GetUsedBytes(), 0u);
    }

    Y_UNIT_TEST(MemoryPerMergedRange)
    {
        TBlockRangeField f(64 * 1024);
        // Add many small ranges that will merge into one
        for (ui64 i = 0; i < 100; ++i) {
            UNIT_ASSERT(f.Add(R(i, i)));
        }

        // Should be a single range [0, 99]
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
        UNIT_ASSERT_GT(f.GetUsedBytes(), 0u);
    }

    Y_UNIT_TEST(MemoryPerLargeRange)
    {
        TBlockRangeField f(4 * 1024);
        // A single large range (same memory as a small one — range data is
        // fixed size)
        UNIT_ASSERT(f.Add(R(0, 1000000)));

        UNIT_ASSERT_GT(f.GetUsedBytes(), 0u);
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(MemoryAfterRemoveAndAdd)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 1000));

        size_t memBefore = f.GetUsedBytes();

        // Remove and add back — memory is NOT freed (pool allocator)
        f.Remove(R(0, 1000));
        f.Add(R(0, 1000));

        size_t memAfter = f.GetUsedBytes();
        // Memory should be higher because pool allocator doesn't free
        UNIT_ASSERT_GE(memAfter, memBefore);
    }

    Y_UNIT_TEST(PoolExhaustionThrows)
    {
        TBlockRangeField f(4 * 1024);
        // Add ranges until the 1 MB pool is exhausted
        size_t count = 0;
        UNIT_ASSERT_EXCEPTION(
            [&]()
            {
                for (ui64 i = 0; i < 100000000; ++i) {
                    f.Add(R(i * 100, i * 100 + 10));
                    ++count;
                }
            }(),
            yexception);
        UNIT_ASSERT_GT(count, 0u);
    }

    Y_UNIT_TEST(PerInstancePools)
    {
        // Each TBlockRangeField has its own pool
        TBlockRangeField f1(4 * 1024);
        TBlockRangeField f2(4 * 1024);

        UNIT_ASSERT(f1.Add(R(0, 10)));
        UNIT_ASSERT(f2.Add(R(0, 10)));

        UNIT_ASSERT_GT(f1.GetUsedBytes(), 0u);
        UNIT_ASSERT_GT(f2.GetUsedBytes(), 0u);
    }

    Y_UNIT_TEST(CopyingIsForbidden)
    {
        static_assert(
            !std::is_copy_constructible_v<TBlockRangeField>,
            "TBlockRangeField must not be copy constructible");
        static_assert(
            !std::is_copy_assignable_v<TBlockRangeField>,
            "TBlockRangeField must not be copy assignable");
    }

    Y_UNIT_TEST(MoveSemantics)
    {
        TBlockRangeField f1(4 * 1024);
        UNIT_ASSERT(f1.Add(R(0, 10)));
        UNIT_ASSERT_VALUES_EQUAL(1u, f1.GetSegmentCount());

        TBlockRangeField f2 = std::move(f1);
        UNIT_ASSERT_VALUES_EQUAL(1u, f2.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL("[0..10]", f2.Print());

        TBlockRangeField f3(4 * 1024);
        UNIT_ASSERT(f3.Add(R(100, 200)));
        f3 = std::move(f2);
        UNIT_ASSERT_VALUES_EQUAL(1u, f3.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL("[0..10]", f3.Print());
    }

    Y_UNIT_TEST(CustomPoolSize)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT_VALUES_EQUAL(4u * 1024u, f.GetPoolSize());

        // A different size should be honored too
        TBlockRangeField big(1024 * 1024);
        UNIT_ASSERT_VALUES_EQUAL(1024u * 1024u, big.GetPoolSize());
    }

    // -------------------------------------------------------------------------
    // Existing functional tests (preserved)

    Y_UNIT_TEST(AddSingleRange)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(10, 20)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(AddTwoNonAdjacentRanges)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), v[1]);
    }

    Y_UNIT_TEST(AddAdjacentRangesMerged)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(6, 10)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 10), v[0]);
    }

    Y_UNIT_TEST(AddOverlappingRangesMerged)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 10)));
        UNIT_ASSERT(f.Add(R(5, 15)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddCoveredByExistingIsNoop)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 100)));
        UNIT_ASSERT(!f.Add(R(10, 20)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 100), v[0]);
    }

    Y_UNIT_TEST(AddCoversMultipleRanges)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT(f.Add(R(20, 25)));
        // New range covers all three and the gaps between them.
        UNIT_ASSERT(f.Add(R(0, 25)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 25), v[0]);
    }

    Y_UNIT_TEST(AddMergesOnBothSides)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        // Bridge the gap.
        UNIT_ASSERT(f.Add(R(5, 10)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddSameRangeTwice)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(3, 7)));
        UNIT_ASSERT(!f.Add(R(3, 7)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(3, 7), v[0]);
    }

    Y_UNIT_TEST(AddField)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 4)));
        UNIT_ASSERT(f.Add(R(20, 24)));

        TBlockRangeField other(4 * 1024);
        UNIT_ASSERT(other.Add(R(5, 10)));
        UNIT_ASSERT(other.Add(R(30, 34)));

        UNIT_ASSERT(f.Add(other));
        UNIT_ASSERT_VALUES_EQUAL("[0..10][20..24][30..34]", f.Print());
        UNIT_ASSERT(!f.Add(other));
        UNIT_ASSERT(!f.Add(f));
    }

    // -------------------------------------------------------------------------
    // Remove – basic

    Y_UNIT_TEST(RemoveFromEmpty)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(!f.Remove(R(0, 10)));   // must not crash, returns false
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveExact)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 10)));
        UNIT_ASSERT(f.Remove(R(0, 10)));
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveFromMiddle)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(5, 10)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 4), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(11, 20), v[1]);
    }

    Y_UNIT_TEST(RemoveLeftPart)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(0, 9)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveRightPart)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(10, 20)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 9), v[0]);
    }

    Y_UNIT_TEST(RemoveNonOverlapping)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(10, 20)));
        UNIT_ASSERT(!f.Remove(R(30, 40)));   // no overlap, no change
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveSeveralRanges)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT(f.Add(R(20, 25)));
        UNIT_ASSERT(f.Remove(R(3, 22)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 2), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(23, 25), v[1]);
    }

    Y_UNIT_TEST(RemoveField)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 40)));

        TBlockRangeField other(4 * 1024);
        UNIT_ASSERT(other.Add(R(5, 9)));
        UNIT_ASSERT(other.Add(R(20, 29)));

        UNIT_ASSERT(f.Remove(other));
        UNIT_ASSERT_VALUES_EQUAL("[0..4][10..19][30..40]", f.Print());
        UNIT_ASSERT(!f.Remove(other));
        UNIT_ASSERT(f.Remove(f));
        UNIT_ASSERT(f.Empty());
    }

    // -------------------------------------------------------------------------
    // Overlaps

    Y_UNIT_TEST(OverlapsOnEmpty)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(!f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsExact)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(10, 20)));
    }

    Y_UNIT_TEST(OverlapsPartialLeft)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(5, 12)));
    }

    Y_UNIT_TEST(OverlapsPartialRight)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(15, 30)));
    }

    Y_UNIT_TEST(OverlapsCovering)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapBefore)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(!f.Overlaps(R(0, 9)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapAfter)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        UNIT_ASSERT(!f.Overlaps(R(21, 30)));
    }

    Y_UNIT_TEST(OverlapsAdjacentNotOverlapping)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));
        // [9,9] touches start but doesn't overlap.
        UNIT_ASSERT(!f.Overlaps(R(5, 9)));
        // [21,21] touches end but doesn't overlap.
        UNIT_ASSERT(!f.Overlaps(R(21, 25)));
    }

    Y_UNIT_TEST(OverlapsField)
    {
        TBlockRangeField left(4 * 1024);
        TBlockRangeField right(4 * 1024);

        UNIT_ASSERT(!left.Overlaps(right));

        left.Add(R(0, 5));
        left.Add(R(20, 25));
        right.Add(R(6, 10));
        right.Add(R(30, 35));
        UNIT_ASSERT(!left.Overlaps(right));
        UNIT_ASSERT(!right.Overlaps(left));

        right.Add(R(24, 29));
        UNIT_ASSERT(left.Overlaps(right));
        UNIT_ASSERT(right.Overlaps(left));
    }

    // -------------------------------------------------------------------------
    // Return value – false cases

    Y_UNIT_TEST(AddReturnsFalseWhenFullyCovered)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 100)));
        // Fully covered by existing interval – no change.
        UNIT_ASSERT(!f.Add(R(10, 20)));
        UNIT_ASSERT(!f.Add(R(0, 100)));
        UNIT_ASSERT(!f.Add(R(50, 50)));
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenEmpty)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(!f.Remove(R(0, 100)));
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenNoOverlap)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(10, 20)));
        // Strictly before.
        UNIT_ASSERT(!f.Remove(R(0, 9)));
        // Strictly after.
        UNIT_ASSERT(!f.Remove(R(21, 30)));
        // Contents unchanged.
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    // -------------------------------------------------------------------------
    // Edge / boundary cases

    Y_UNIT_TEST(AddStartingAtZero)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 0)));
        UNIT_ASSERT(f.Add(R(1, 5)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
    }

    Y_UNIT_TEST(RemoveSingleBlock)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(0, 4)));
        UNIT_ASSERT(f.Remove(R(2, 2)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 1), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(3, 4), v[1]);
    }

    Y_UNIT_TEST(ManyFragmentsAfterRemoves)
    {
        TBlockRangeField f(64 * 1024);
        UNIT_ASSERT(f.Add(R(0, 99)));
        // Remove every even block to create 50 gaps.
        for (ui64 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.Remove(R(i, i)));
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(50u, v.size());
        for (ui64 i = 0; i < 50; ++i) {
            ui64 odd = i * 2 + 1;
            UNIT_ASSERT_VALUES_EQUAL(R(odd, odd), v[i]);
        }
    }

    Y_UNIT_TEST(AddRestoresAfterRemoves)
    {
        TBlockRangeField f(64 * 1024);
        UNIT_ASSERT(f.Add(R(0, 99)));
        for (ui64 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.Remove(R(i, i)));
        }
        // Adding back should merge everything.
        for (ui64 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.Add(R(i, i)));
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 99), v[0]);
    }

    Y_UNIT_TEST(EnumerateOrderedByStart)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT(f.Add(R(50, 60)));
        UNIT_ASSERT(f.Add(R(10, 20)));
        UNIT_ASSERT(f.Add(R(30, 40)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT(v[0].Start < v[1].Start);
        UNIT_ASSERT(v[1].Start < v[2].Start);
    }

    Y_UNIT_TEST(EnumerateStopsAtCondition)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 5));
        f.Add(R(10, 15));
        f.Add(R(20, 25));

        TVector<TBlockRange64> seen;
        f.Enumerate(
            [&](TBlockRange64 r)
            {
                seen.push_back(r);
                return r.Start >= 10
                           ? TBlockRangeField::EEnumerateContinuation::Stop
                           : TBlockRangeField::EEnumerateContinuation::Continue;
            });

        // Visits first two ranges, then stops on the second.
        UNIT_ASSERT_VALUES_EQUAL(2u, seen.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), seen[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), seen[1]);
    }

    // -------------------------------------------------------------------------
    // GetBlockCount / GetSegmentCount

    Y_UNIT_TEST(CountersOnEmpty)
    {
        TBlockRangeField f(4 * 1024);
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterSingleAdd)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(10, 20));   // 11 blocks, 1 segment
        UNIT_ASSERT_VALUES_EQUAL(11u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterTwoDisjointAdds)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 4));     // 5 blocks
        f.Add(R(10, 14));   // 5 blocks → total 10, 2 segments
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterMerge)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 4));
        f.Add(R(5, 9));   // adjacent – merges into [0,9], 10 blocks, 1 segment
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterRemove)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 19));     // 20 blocks, 1 segment
        f.Remove(R(5, 9));   // removes 5 blocks from the middle → 2 segments
        UNIT_ASSERT_VALUES_EQUAL(15u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterClear)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(0, 9));
        f.Add(R(20, 29));
        f.Clear();
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersSingleBlock)
    {
        TBlockRangeField f(4 * 1024);
        f.Add(R(42, 42));
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersManyFragments)
    {
        TBlockRangeField f(64 * 1024);
        f.Add(R(0, 99));
        // Remove every even block → 50 single-block segments.
        for (ui64 i = 0; i < 100; i += 2) {
            f.Remove(R(i, i));
        }
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetSegmentCount());
    }
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
