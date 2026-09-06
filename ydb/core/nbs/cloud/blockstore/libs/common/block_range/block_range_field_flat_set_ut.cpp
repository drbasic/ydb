#include "block_range_field_flat_set.h"

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/vector.h>
#include <util/string/builder.h>

namespace NYdb::NBS::NBlockStore {

namespace {

TVector<TBlockRangeFieldFlatSet::TRange> Collect(
    const TBlockRangeFieldFlatSet& field)
{
    TVector<TBlockRangeFieldFlatSet::TRange> result;
    field.Enumerate(
        [&](TBlockRangeFieldFlatSet::TRange r)
        {
            result.push_back(r);
            return TBlockRangeFieldFlatSet::EEnumerateContinuation::Continue;
        });
    return result;
}

TBlockRangeFieldFlatSet::TRange R(ui16 start, ui16 end)
{
    return TBlockRangeFieldFlatSet::TRange::MakeClosedInterval(start, end);
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TBlockRangeFieldFlatSetTest)
{
    // -------------------------------------------------------------------------
    // Basic Add

    Y_UNIT_TEST(AddSingleRange)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(AddTwoNonAdjacentRanges)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), v[1]);
    }

    Y_UNIT_TEST(AddAdjacentRangesMerged)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(f.TryAdd(R(6, 10), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..10]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 10), v[0]);
    }

    Y_UNIT_TEST(AddOverlappingRangesMerged)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 10), &changed));
        UNIT_ASSERT(f.TryAdd(R(5, 15), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddCoveredByExistingIsNoop)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        UNIT_ASSERT(!f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 100), v[0]);
    }

    Y_UNIT_TEST(AddCoversMultipleRanges)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(f.TryAdd(R(20, 25), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15][20..25]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(0, 25), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 25), v[0]);
    }

    Y_UNIT_TEST(AddMergesOnBothSides)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(5, 10), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddSameRangeTwice)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(3, 7), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        UNIT_ASSERT(!f.TryAdd(R(3, 7), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(3, 7), v[0]);
    }

    Y_UNIT_TEST(AddField)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 4), &changed));
        UNIT_ASSERT(f.TryAdd(R(20, 24), &changed));

        TBlockRangeFieldFlatSet other;
        bool otherChanged = false;
        UNIT_ASSERT(other.TryAdd(R(5, 10), &otherChanged));
        UNIT_ASSERT(other.TryAdd(R(30, 34), &otherChanged));

        // Merge ranges from other into f via enumerate
        other.Enumerate(
            [&](TBlockRangeFieldFlatSet::TRange r)
            {
                f.TryAdd(r, &changed);
                return TBlockRangeFieldFlatSet::EEnumerateContinuation::
                    Continue;
            });
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
    }

    // -------------------------------------------------------------------------
    // Remove

    Y_UNIT_TEST(RemoveFromEmpty)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(!f.TryRemove(R(0, 10), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveExact)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 10), &changed);
        UNIT_ASSERT(f.TryRemove(R(0, 10), &changed));
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveFromMiddle)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 20), &changed);
        UNIT_ASSERT(f.TryRemove(R(5, 10), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..4][11..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 4), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(11, 20), v[1]);
    }

    Y_UNIT_TEST(RemoveLeftPart)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 20), &changed);
        UNIT_ASSERT(f.TryRemove(R(0, 9), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveRightPart)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 20), &changed);
        UNIT_ASSERT(f.TryRemove(R(10, 20), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..9]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 9), v[0]);
    }

    Y_UNIT_TEST(RemoveNonOverlapping)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(!f.TryRemove(R(30, 40), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveSeveralRanges)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 5), &changed);
        f.TryAdd(R(10, 15), &changed);
        f.TryAdd(R(20, 25), &changed);
        UNIT_ASSERT(f.TryRemove(R(3, 22), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..2][23..25]", f.Print());
    }

    // -------------------------------------------------------------------------
    // Overlaps

    Y_UNIT_TEST(OverlapsOnEmpty)
    {
        TBlockRangeFieldFlatSet f;
        UNIT_ASSERT(!f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsExact)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(f.Overlaps(R(10, 20)));
    }

    Y_UNIT_TEST(OverlapsPartialLeft)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(f.Overlaps(R(5, 12)));
    }

    Y_UNIT_TEST(OverlapsPartialRight)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(f.Overlaps(R(15, 30)));
    }

    Y_UNIT_TEST(OverlapsCovering)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapBefore)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(!f.Overlaps(R(0, 9)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapAfter)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(!f.Overlaps(R(21, 30)));
    }

    Y_UNIT_TEST(OverlapsAdjacentNotOverlapping)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(!f.Overlaps(R(5, 9)));
        UNIT_ASSERT(!f.Overlaps(R(21, 25)));
    }

    // -------------------------------------------------------------------------
    // Return value semantics

    Y_UNIT_TEST(AddReturnsFalseWhenFullyCovered)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 100), &changed);
        UNIT_ASSERT(!f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(!f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(!f.TryAdd(R(50, 50), &changed));
        UNIT_ASSERT(!changed);
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenEmpty)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        UNIT_ASSERT(!f.TryRemove(R(0, 100), &changed));
        UNIT_ASSERT(!changed);
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenNoOverlap)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT(!f.TryRemove(R(0, 9), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(!f.TryRemove(R(21, 30), &changed));
        UNIT_ASSERT(!changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    // -------------------------------------------------------------------------
    // Edge / boundary cases

    Y_UNIT_TEST(AddStartingAtZero)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 0), &changed);
        f.TryAdd(R(1, 5), &changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
    }

    Y_UNIT_TEST(RemoveSingleBlock)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 4), &changed);
        UNIT_ASSERT(f.TryRemove(R(2, 2), &changed));
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 1), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(3, 4), v[1]);
    }

    Y_UNIT_TEST(ManyFragmentsAfterRemoves)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 99), &changed);
        for (ui16 i = 0; i < 100; i += 2) {
            f.TryRemove(R(i, i), &changed);
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
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 99), &changed);
        for (ui16 i = 0; i < 100; i += 2) {
            f.TryRemove(R(i, i), &changed);
        }
        for (ui16 i = 0; i < 100; i += 2) {
            f.TryAdd(R(i, i), &changed);
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 99), v[0]);
    }

    Y_UNIT_TEST(EnumerateOrderedByStart)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(50, 60), &changed);
        f.TryAdd(R(10, 20), &changed);
        f.TryAdd(R(30, 40), &changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT(v[0].Start < v[1].Start);
        UNIT_ASSERT(v[1].Start < v[2].Start);
    }

    Y_UNIT_TEST(EnumerateStopsAtCondition)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 5), &changed);
        f.TryAdd(R(10, 15), &changed);
        f.TryAdd(R(20, 25), &changed);

        TVector<TBlockRangeFieldFlatSet::TRange> seen;
        f.Enumerate(
            [&](TBlockRangeFieldFlatSet::TRange r)
            {
                seen.push_back(r);
                return r.Start >= 10 ? TBlockRangeFieldFlatSet::
                                           EEnumerateContinuation::Stop
                                     : TBlockRangeFieldFlatSet::
                                           EEnumerateContinuation::Continue;
            });

        UNIT_ASSERT_VALUES_EQUAL(2u, seen.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), seen[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), seen[1]);
    }

    // -------------------------------------------------------------------------
    // Counters

    Y_UNIT_TEST(CountersOnEmpty)
    {
        TBlockRangeFieldFlatSet f;
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterSingleAdd)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        UNIT_ASSERT_VALUES_EQUAL(11u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterTwoDisjointAdds)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 4), &changed);
        f.TryAdd(R(10, 14), &changed);
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterMerge)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 4), &changed);
        f.TryAdd(R(5, 9), &changed);
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterRemove)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 19), &changed);
        f.TryRemove(R(5, 9), &changed);
        UNIT_ASSERT_VALUES_EQUAL(15u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterClear)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 9), &changed);
        f.TryAdd(R(20, 29), &changed);
        f.Clear();
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersSingleBlock)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(42, 42), &changed);
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersManyFragments)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 99), &changed);
        for (ui16 i = 0; i < 100; i += 2) {
            f.TryRemove(R(i, i), &changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetSegmentCount());
    }

    // -------------------------------------------------------------------------
    // Capacity exhaustion & reuse

    Y_UNIT_TEST(CapacityExhaustion)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        // Add 64 disjoint ranges (max capacity = 64 segments)
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        // 65th range should fail
        UNIT_ASSERT(!f.TryAdd(R(700, 705), &changed));
        UNIT_ASSERT(f.OutOfMemory());
    }

    Y_UNIT_TEST(ReuseAfterClear)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        // Fill capacity
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());

        f.Clear();
        UNIT_ASSERT(f.Empty());
        UNIT_ASSERT(!f.OutOfMemory());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());

        // Should be able to add again
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(OutOfMemoryFlagPreventsAdds)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        // Fill capacity
        for (ui16 i = 0; i < 64; ++i) {
            f.TryAdd(R(i * 10, i * 10 + 5), &changed);
        }
        UNIT_ASSERT(f.OutOfMemory());
        // All further adds should fail
        UNIT_ASSERT(!f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT(!f.TryAdd(R(1000, 1010), &changed));
    }

    // -------------------------------------------------------------------------
    // Add range that extends predecessor only

    Y_UNIT_TEST(AddExtendsPredecessor)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        f.TryAdd(R(5, 15), &changed);
        UNIT_ASSERT_VALUES_EQUAL("[5..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(5, 20), v[0]);
    }

    // -------------------------------------------------------------------------
    // Add range that extends successor only

    Y_UNIT_TEST(AddExtendsSuccessor)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(10, 20), &changed);
        f.TryAdd(R(15, 30), &changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..30]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 30), v[0]);
    }

    // -------------------------------------------------------------------------
    // Insert in the middle without overlap

    Y_UNIT_TEST(AddNonAdjacentInMiddle)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 5), &changed);
        f.TryAdd(R(20, 25), &changed);
        f.TryAdd(R(10, 15), &changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15][20..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), v[1]);
        UNIT_ASSERT_VALUES_EQUAL(R(20, 25), v[2]);
    }

    // -------------------------------------------------------------------------
    // Remove that splits a range into two parts

    Y_UNIT_TEST(RemoveSplitsRange)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 100), &changed);
        UNIT_ASSERT(f.TryRemove(R(30, 60), &changed));
        UNIT_ASSERT_VALUES_EQUAL("[0..29][61..100]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 29), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(61, 100), v[1]);
    }

    // -------------------------------------------------------------------------
    // Multiple remove operations in sequence

    Y_UNIT_TEST(MultipleSequentialRemoves)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        f.TryAdd(R(0, 99), &changed);
        f.TryRemove(R(0, 9), &changed);
        f.TryRemove(R(20, 29), &changed);
        f.TryRemove(R(50, 59), &changed);
        f.TryRemove(R(80, 89), &changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..19][30..49][60..79][90..99]", f.Print());
    }

    // -------------------------------------------------------------------------
    // Print on empty

    Y_UNIT_TEST(PrintEmpty)
    {
        TBlockRangeFieldFlatSet f;
        UNIT_ASSERT(f.Print().empty());
    }

    // -------------------------------------------------------------------------
    // Clear resets all state

    Y_UNIT_TEST(ClearResetsOutOfMemory)
    {
        TBlockRangeFieldFlatSet f;
        bool changed = false;
        for (ui16 i = 0; i < 64; ++i) {
            f.TryAdd(R(i * 10, i * 10 + 5), &changed);
        }
        UNIT_ASSERT(f.OutOfMemory());
        f.Clear();
        UNIT_ASSERT(!f.OutOfMemory());
        UNIT_ASSERT(f.Empty());
    }

}   // Y_UNIT_TEST_SUITE

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
