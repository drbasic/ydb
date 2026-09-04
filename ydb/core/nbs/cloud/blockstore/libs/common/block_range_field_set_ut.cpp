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

bool OverlapsWith(
    const TBlockRangeFieldSet& left,
    const TBlockRangeFieldSet& right)
{
    bool result = false;
    left.Enumerate(
        [&](TBlockRangeFieldSet::TRange r)
        {
            if (right.Overlaps(r)) {
                result = true;
                return TBlockRangeFieldSet::EEnumerateContinuation::Stop;
            }
            return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
        });
    return result;
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
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(10, 15), v[1]);
    }

    Y_UNIT_TEST(AddAdjacentRangesMerged)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(6, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..10]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 10), v[0]);
    }

    Y_UNIT_TEST(AddOverlappingRangesMerged)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(5, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddCoveredByExistingIsNoop)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..100]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 100), v[0]);
    }

    Y_UNIT_TEST(AddCoversMultipleRanges)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(20, 25), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15][20..25]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(0, 25), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 25), v[0]);
    }

    Y_UNIT_TEST(AddMergesOnBothSides)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..5][10..15]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(5, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..15]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 15), v[0]);
    }

    Y_UNIT_TEST(AddSameRangeTwice)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(3, 7), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        UNIT_ASSERT(f.TryAdd(R(3, 7), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[3..7]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(3, 7), v[0]);
    }

    Y_UNIT_TEST(AddField)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 4), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(20, 24), &changed));
        UNIT_ASSERT(changed);

        TBlockRangeFieldSet other;
        bool otherChanged = false;
        UNIT_ASSERT(other.TryAdd(R(5, 10), &otherChanged));
        UNIT_ASSERT(otherChanged);
        UNIT_ASSERT(other.TryAdd(R(30, 34), &otherChanged));
        UNIT_ASSERT(otherChanged);

        // Merge ranges from other into f via enumerate.
        other.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryAdd(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());

        // Re-merging the same ranges should not change anything.
        other.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryAdd(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(!changed);

        // Self-merge is a no-op.
        f.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryAdd(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(!changed);
    }

    // -------------------------------------------------------------------------
    // Remove

    Y_UNIT_TEST(RemoveFromEmpty)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryRemove(R(0, 10), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveExact)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(0, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.Print().empty());
        UNIT_ASSERT(Collect(f).empty());
    }

    Y_UNIT_TEST(RemoveFromMiddle)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(5, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..4][11..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 4), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(11, 20), v[1]);
    }

    Y_UNIT_TEST(RemoveLeftPart)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(0, 9), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveRightPart)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..9]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 9), v[0]);
    }

    Y_UNIT_TEST(RemoveNonOverlapping)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(30, 40), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT_VALUES_EQUAL("[10..20]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    Y_UNIT_TEST(RemoveSeveralRanges)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(20, 25), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(3, 22), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..2][23..25]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 2), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(23, 25), v[1]);
    }

    Y_UNIT_TEST(RemoveField)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 40), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..40]", f.Print());

        TBlockRangeFieldSet other;
        bool otherChanged = false;
        UNIT_ASSERT(other.TryAdd(R(5, 9), &otherChanged));
        UNIT_ASSERT(otherChanged);
        UNIT_ASSERT(other.TryAdd(R(20, 29), &otherChanged));
        UNIT_ASSERT(otherChanged);

        other.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryRemove(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL("[0..4][10..19][30..40]", f.Print());
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());

        // Re-removing the same ranges should not change anything.
        other.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryRemove(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(!changed);

        // Removing f from itself should empty the field.
        f.Enumerate(
            [&](TBlockRangeFieldSet::TRange r)
            {
                f.TryRemove(r, &changed);
                return TBlockRangeFieldSet::EEnumerateContinuation::Continue;
            });
        UNIT_ASSERT(changed);
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
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.Overlaps(R(10, 20)));
    }

    Y_UNIT_TEST(OverlapsPartialLeft)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.Overlaps(R(5, 12)));
    }

    Y_UNIT_TEST(OverlapsPartialRight)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.Overlaps(R(15, 30)));
    }

    Y_UNIT_TEST(OverlapsCovering)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.Overlaps(R(0, 100)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapBefore)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(!f.Overlaps(R(0, 9)));
    }

    Y_UNIT_TEST(OverlapsNoOverlapAfter)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(!f.Overlaps(R(21, 30)));
    }

    Y_UNIT_TEST(OverlapsAdjacentNotOverlapping)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(!f.Overlaps(R(5, 9)));
        UNIT_ASSERT(!f.Overlaps(R(21, 25)));
    }

    Y_UNIT_TEST(OverlapsField)
    {
        TBlockRangeFieldSet left, right;
        UNIT_ASSERT(!OverlapsWith(left, right));

        bool changed = false;
        UNIT_ASSERT(left.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(left.TryAdd(R(20, 25), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(right.TryAdd(R(6, 10), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(right.TryAdd(R(30, 35), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(!OverlapsWith(left, right));
        UNIT_ASSERT(!OverlapsWith(right, left));

        UNIT_ASSERT(right.TryAdd(R(24, 29), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(OverlapsWith(left, right));
        UNIT_ASSERT(OverlapsWith(right, left));
    }

    // -------------------------------------------------------------------------
    // Return value semantics

    Y_UNIT_TEST(AddReturnsFalseWhenFullyCovered)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(f.TryAdd(R(0, 100), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(f.TryAdd(R(50, 50), &changed));
        UNIT_ASSERT(!changed);
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenEmpty)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryRemove(R(0, 100), &changed));
        UNIT_ASSERT(!changed);
    }

    Y_UNIT_TEST(RemoveReturnsFalseWhenNoOverlap)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(f.TryRemove(R(0, 9), &changed));
        UNIT_ASSERT(!changed);
        UNIT_ASSERT(f.TryRemove(R(21, 30), &changed));
        UNIT_ASSERT(!changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(10, 20), v[0]);
    }

    // -------------------------------------------------------------------------
    // Edge / boundary cases

    Y_UNIT_TEST(AddStartingAtZero)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 0), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(1, 5), &changed));
        UNIT_ASSERT(changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 5), v[0]);
    }

    Y_UNIT_TEST(RemoveSingleBlock)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 4), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(2, 2), &changed));
        UNIT_ASSERT(changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(2u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 1), v[0]);
        UNIT_ASSERT_VALUES_EQUAL(R(3, 4), v[1]);
    }

    Y_UNIT_TEST(ManyFragmentsAfterRemoves)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 99), &changed));
        UNIT_ASSERT(changed);
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.TryRemove(R(i, i), &changed));
            UNIT_ASSERT(changed);
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
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 99), &changed));
        UNIT_ASSERT(changed);
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.TryRemove(R(i, i), &changed));
            UNIT_ASSERT(changed);
        }
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.TryAdd(R(i, i), &changed));
            UNIT_ASSERT(changed);
        }
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(1u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 99), v[0]);
    }

    Y_UNIT_TEST(EnumerateOrderedByStart)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(50, 60), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(30, 40), &changed));
        UNIT_ASSERT(changed);
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(3u, v.size());
        UNIT_ASSERT(v[0].Start < v[1].Start);
        UNIT_ASSERT(v[1].Start < v[2].Start);
    }

    Y_UNIT_TEST(EnumerateStopsAtCondition)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 5), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 15), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(20, 25), &changed));
        UNIT_ASSERT(changed);

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
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(10, 20), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL(11u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterTwoDisjointAdds)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 4), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(10, 14), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterMerge)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 4), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(5, 9), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL(10u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterRemove)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 19), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryRemove(R(5, 9), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL(15u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(2u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersAfterClear)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 9), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT(f.TryAdd(R(20, 29), &changed));
        UNIT_ASSERT(changed);
        f.Clear();
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersSingleBlock)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(42, 42), &changed));
        UNIT_ASSERT(changed);
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(CountersManyFragments)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(0, 99), &changed));
        UNIT_ASSERT(changed);
        for (ui16 i = 0; i < 100; i += 2) {
            UNIT_ASSERT(f.TryRemove(R(i, i), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetBlockCount());
        UNIT_ASSERT_VALUES_EQUAL(50u, f.GetSegmentCount());
    }

    // -------------------------------------------------------------------------
    // Arena exhaustion & reuse

    Y_UNIT_TEST(ArenaExhaustion)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        // Add 64 disjoint ranges (max for 512-byte arena = 64 nodes)
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(AddMergeFailsRecalculatesCounters)
    {
        // Fill arena with 64 disjoint ranges of 3 blocks each.
        TBlockRangeFieldSet f;
        bool changed = false;
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 2), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL(192u, f.GetBlockCount());   // 64 * 3

        // TryAdd a range that merges all 64 nodes — removes them all from tree,
        // then AllocNode fails (arena full). RecalculateCounters must fix
        // counters.
        UNIT_ASSERT(!f.TryAdd(R(32767, 32767), &changed));

        // Counters must reflect the tree as-is (no merge happened).
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL(192u, f.GetBlockCount());

        // Tree content must be unchanged.
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(64u, v.size());
    }

    Y_UNIT_TEST(RemoveSplitFailsRecalculatesCounters)
    {
        // Fill arena with 64 disjoint ranges of 3 blocks each.
        TBlockRangeFieldSet f;
        bool changed = false;
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 2), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL(192u, f.GetBlockCount());   // 64 * 3

        // Remove the middle block of range [0..2] → requires splitting into
        // [0..0] + [2..2] (new node), but arena is full.
        // BlockCount is modified BEFORE AllocNode is called — this is the
        // invariant-violation point. RecalculateCounters must fix it.
        UNIT_ASSERT(!f.TryRemove(R(1, 1), &changed));

        // Counters must reflect the tree as-is (nothing was removed).
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
        UNIT_ASSERT_VALUES_EQUAL(192u, f.GetBlockCount());

        // Tree content must be unchanged.
        auto v = Collect(f);
        UNIT_ASSERT_VALUES_EQUAL(64u, v.size());
        UNIT_ASSERT_VALUES_EQUAL(R(0, 2), v[0]);
    }

    Y_UNIT_TEST(ReuseAfterClear)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        // Fill arena
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());

        f.Clear();
        UNIT_ASSERT(f.Empty());
        UNIT_ASSERT_VALUES_EQUAL(0u, f.GetSegmentCount());

        // Should be able to add again
        for (ui16 i = 0; i < 64; ++i) {
            UNIT_ASSERT(f.TryAdd(R(i * 10, i * 10 + 5), &changed));
            UNIT_ASSERT(changed);
        }
        UNIT_ASSERT_VALUES_EQUAL(64u, f.GetSegmentCount());
    }

    Y_UNIT_TEST(MemoryPerRange)
    {
        TBlockRangeFieldSet f;
        bool changed = false;
        UNIT_ASSERT(f.TryAdd(R(100, 200), &changed));
        UNIT_ASSERT(changed);
        // Node size is 8 bytes, so one range = 8 bytes
        UNIT_ASSERT_VALUES_EQUAL(1u, f.GetSegmentCount());
    }
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
