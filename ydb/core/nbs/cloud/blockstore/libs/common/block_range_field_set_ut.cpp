#include "block_range_field_set.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/vector.h>
#include <util/string/builder.h>

namespace NYdb::NBS::NBlockStore {

namespace {

TVector<TBlockRangeFieldSet::TRange> Collect(const TBlockRangeFieldSet& field)
{
    TVector<TBlockRangeFieldSet::TRange> result;
    field.Enumerate(
        [&](TBlockRangeFieldSet::TRange r)
        {
            result.push_back(r);
            return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
        });
    return result;
}

TBlockRangeFieldSet::TRange R(ui16 start, ui16 end)
{
    return TBlockRangeFieldSet::TRange::MakeClosedInterval(start, end);
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TBlockRangeFieldSetTest)
{
    // -------------------------------------------------------------------------
    // Basic Add

    Y_UNIT_TEST(AddSingleRange)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(10, 20)));
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(AddTwoNonAdjacentRanges)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), v[1]);
    }

    Y_UNIT_TEST(AddAdjacentRangesMerged)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(6, 10)));
        UNIT_ASSERT_VALUES_EQUAL("[0..10]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 10), v[0]);
    }

    Y_UNIT_TEST(AddOverlappingRangesMerged)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 10)));
        UNIT_ASSERT(f.Add(R(5, 15)));
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddCoveredByExistingIsNoop)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 100)));
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        UNIT_ASSERT(!f.Add(R(10, 20)));
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 100), v[0]);
    }

    Y_UNIT_TEST(AddCoversMultipleRanges)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT(f.Add(R(20, 25)));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15][20..25]", f.Print());
        UNIT_ASSERT(f.Add(R(0, 25)));
        UNIT_ASSERT_VALUES_EQUAL("[0..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 25), v[0]);
    }

    Y_UNIT_TEST(AddMergesOnBothSides)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        UNIT_ASSERT(f.Add(R(5, 10)));
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddSameRangeTwice)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(3, 7)));
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        UNIT_ASSERT(!f.Add(R(3, 7)));
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(3, 7), v[0]);
    }

    Y_UNIT_TEST(AddField)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 4)));
        UNIT_ASSERT(f.Add(R(20, 24)));

        TBlockRangeFieldSet other;
        UNIT_ASSERT(other.Add(R(5, 10)));
        UNIT_ASSERT(other.Add(R(30, 34)));

        UNIT_ASSERT(f.Add(other));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT(!f.Add(other));
        UNIT_ASSERT(!f.Add(f));
    }

    // -------------------------------------------------------------------------
    // Remove

    Y_UNIT_TEST(RemoveFromEmpty)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(!f.Remove(R(0, 10)));
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveExact)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 10)));
        UNIT_ASSERT(f.Remove(R(0, 10)));
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveFromMiddle)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(5, 10)));
        UNIT_ASSERT_VALUES_EQUAL("[0..4][11..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 4), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(11, 20), v[1]);
    }

    Y_UNIT_TEST(RemoveLeftPart)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(0, 9)));
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveRightPart)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 20)));
        UNIT_ASSERT(f.Remove(R(10, 20)));
        UNIT_ASSERT_VALUES_EQUAL("[0..9]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 9), v[0]);
    }

    Y_UNIT_TEST(RemoveNonOverlapping)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(10, 20)));
        UNIT_ASSERT(!f.Remove(R(30, 40)));
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveSeveralRanges)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 5)));
        UNIT_ASSERT(f.Add(R(10, 15)));
        UNIT_ASSERT(f.Add(R(20, 25)));
        UNIT_ASSERT(f.Remove(R(3, 22)));
        UNIT_ASSERT_VALUES_EQUAL("[0..2][23..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 2), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(23, 25), v[1]);
    }

    Y_UNIT_TEST(RemoveField)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 40)));
        UNIT_ASSERT_VALUES_EQUAL("[0..40]", f.Print());

        TBlockRangeFieldSet other;
        UNIT_ASSERT(other.Add(R(5, 9)));
        UNIT_ASSERT(other.Add(R(20, 29)));

        UNIT_ASSERT(f.Remove(other));
        UNIT_ASSERT_VALUES_EQUAL("[0..4][10..19][30..40]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT(!f.Remove(other));
        UNIT_ASSERT(f.Remove(f));
        UNIT_ASSERT(f.Empty());
    }

    // -------------------------------------------------------------------------
    // Overlaps

    Y_UNIT_TEST(OverlapsOnEmpty)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(!f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsExact)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(10, 20)));
    }

    Y_UNIT_TEST(OverlapsPartialLeft)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(5, 12)));
    }

    Y_UNIT_TEST(OverlapsPartialRight)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(15, 30)));
    }

    Y_UNIT_TEST(OverlapsCovering)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapBefore)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(!f.Overlaps(R(0, 9)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapAfter)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(!f.Overlaps(R(21, 30)));
    }

    Y_UNIT_TEST(OverlapsAdjacentNotOverlapping)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT(!f.Overlaps(R(5, 9)));
        UNIT_ASSERT(!f.Overlaps(R(21, 25)));
    }

    Y_UNIT_TEST(OverlapsField)
    {
        TBlockRangeFieldSet left, right;
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
    // Return value semantics

    Y_UNIT_TEST(AddReturnsFalseWhenFullyCovered)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 100)));
        UNIT_ASSERT(!f.Add(R(10, 20)));
        UNIT_ASSERT(!f.Add(R(0, 100)));
        UNIT_ASSERT(!f.Add(R(50, 50)));
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenEmpty)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(!f.Remove(R(0, 100)));
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenNoOverlap)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(10, 20)));
        UNIT_ASSERT(!f.Remove(R(0, 9)));
        UNIT_ASSERT(!f.Remove(R(21, 30)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    // -------------------------------------------------------------------------
    // Edge / boundary cases

    Y_UNIT_TEST(AddStartingAtZero)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 0)));
        UNIT_ASSERT(f.Add(R(1, 5)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
    }

    Y_UNIT_TEST(RemoveSingleBlock)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 4)));
        UNIT_ASSERT(f.Remove(R(2, 2)));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 1), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(3, 4), v[1]);
    }

    Y_UNIT_TEST(ManyFragmentsAfterRemoves)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 99)));
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.Remove(R(i, i)));
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(50u, v.size());
        for (ui16 i = 0; i < 50; ++i) {
            ui16 odd = i * 2 + 1;
            UNIT_ASSERT_VALUES_EQUAL(R(odd, odd), v[i]);
        }
    }

    Y_UNIT_TEST(AddRestoresAfterRemoves)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT(f.Add(R(0, 99)));
        for (ui16 i = 0; i < 100; i += 2) {
            f.Remove(R(i, i));
        }
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.Add(R(i, i)));
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 99), v[0]);
    }

    Y_UNIT_TEST(EnumerateOrderedByStart)
    {
        TBlockRangeFieldSet f;
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
        TBlockRangeFieldSet f;
        f.Add(R(0, 5));
        f.Add(R(10, 15));
        f.Add(R(20, 25));

        TVector<TBlockRangeFieldSet::TRange> seen;
        f.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                seen.push_back(r);
                return r.Start >= 10
                           ? TBlockRangeFieldSet::EEnumerateContinuation::Stop
                           : TBlockRangeFieldSet::EEnumerateContinuation::
                                 Continue;
            });

        UNIT_ASSERT_VALUES_EQUAL(2u, seen.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), seen[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), seen[1]);
    }

    // -------------------------------------------------------------------------
    // Counters

    Y_UNIT_TEST(CountersOnEmpty)
    {
        TBlockRangeFieldSet f;
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterSingleAdd)
    {
        TBlockRangeFieldSet f;
        f.Add(R(10, 20));
        UNIT_ASSERT_VALUES_EQUAL(11u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterTwoDisjointAdds)
    {
        TBlockRangeFieldSet f;
        f.Add(R(0, 4));
        f.Add(R(10, 14));
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterMerge)
    {
        TBlockRangeFieldSet f;
        f.Add(R(0, 4));
        f.Add(R(5, 9));
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterRemove)
    {
        TBlockRangeFieldSet f;
        f.Add(R(0, 19));
        f.Remove(R(5, 9));
        UNIT_ASSERT_VALUES_EQUAL(15u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterClear)
    {
        TBlockRangeFieldSet f;
        f.Add(R(0, 9));
        f.Add(R(20, 29));
        f.Clear();
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersSingleBlock)
    {
        TBlockRangeFieldSet f;
        f.Add(R(42, 42));
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersManyFragments)
    {
        TBlockRangeFieldSet f;
        f.Add(R(0, 99));
        for (ui16 i = 0; i < 100; i += 2) {
            f.Remove(R(i, i));
        }
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetSegmentCount());
    }

    // -------------------------------------------------------------------------
    // Arena exhaustion & reuse

    Y_UNIT_TEST(ArenaExhaustion)
    {
        TBlockRangeFieldSet f;
        // Add 64 disjoint ranges (max for 512-byte arena = 64 nodes)
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.Add(R(i * 10, i * 10 + 5)));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        // 65th range should fail
        UNIT_ASSERT(!f.Add(R(700, 705)));
        UNIT_ASSERT(f.OutOfMemory());
    }

    Y_UNIT_TEST(ReuseAfterClear)
    {
        TBlockRangeFieldSet f;
        // Fill arena
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.Add(R(i * 10, i * 10 + 5)));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());

        f.Clear();
        UNIT_ASSERT(f.Empty());
        UNIT_ASSERT(!f.OutOfMemory());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());

        // Should be able to add again
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.Add(R(i * 10, i * 10 + 5)));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(MemoryPerRange)
    {
        TBlockRangeFieldSet f;
        f.Add(R(100, 200));
        // Node size is 8 bytes, so one range = 8 bytes
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
