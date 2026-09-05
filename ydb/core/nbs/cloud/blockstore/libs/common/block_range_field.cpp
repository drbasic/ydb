#include "block_range_field.h"

#include "block_range_allocator.h"
#include "block_range_field_std_set.h"

#include <utility>

namespace NYdb::NBS::NBlockStore {

namespace {

////////////////////////////////////////////////////////////////////////////////

void AddRanges(const IBlockRangeFieldImpl& source, IBlockRangeFieldImpl* target)
{
    source.Enumerate(
        [&](TBlockRange16 range)
        {
            bool changed = false;
            Y_ABORT_UNLESS(target->TryAdd(range, &changed));
            return IBlockRangeFieldImpl::EEnumerateContinuation::Continue;
        });
}

std::unique_ptr<IBlockRangeFieldImpl> MakeStdSetImpl(
    ui16 maxBlockCount,
    const IBlockRangeFieldImpl& source,
    std::unique_ptr<TBlockRangePool>& pool)
{
    if (!pool) {
        pool =
            std::make_unique<TBlockRangePool>(DefaultBlockRangePoolChunkSize);
    }
    auto result =
        std::make_unique<TBlockRangeFieldStdSet>(maxBlockCount, pool.get());
    AddRanges(source, result.get());
    return result;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

TBlockRangeField::TBlockRangeField(ui16 maxBlockCount)
    : MaxBlockCount(maxBlockCount)
{}

TBlockRangeField::~TBlockRangeField() = default;

TBlockRangeField::TBlockRangeField(TBlockRangeField&& other) noexcept
    : MaxBlockCount(other.MaxBlockCount)
    , Simple(std::move(other.Simple))
    , Pool(std::move(other.Pool))
    , Impl(std::move(other.Impl))
{}

TBlockRangeField& TBlockRangeField::operator=(TBlockRangeField&& other) noexcept
{
    MaxBlockCount = other.MaxBlockCount;
    Simple = std::move(other.Simple);
    Pool = std::move(other.Pool);
    Impl = std::move(other.Impl);
    return *this;
}

////////////////////////////////////////////////////////////////////////////////

bool TBlockRangeField::Add(TBlockRange16 range)
{
    bool changed = false;
    if (Impl) {
        Y_ABORT_UNLESS(Impl->TryAdd(range, &changed));
        return changed;
    }

    if (Simple.TryAdd(range, &changed)) {
        return changed;
    }

    Impl = MakeStdSetImpl(MaxBlockCount, Simple, Pool);
    Y_ABORT_UNLESS(Impl->TryAdd(range, &changed));
    Simple.Clear();
    return changed;
}

bool TBlockRangeField::Add(const TBlockRangeField& field)
{
    if (this == &field) {
        return false;
    }

    bool changed = false;
    field.Enumerate(
        [&](TBlockRange16 range)
        {
            changed |= Add(range);
            return EEnumerateContinuation::Continue;
        });
    return changed;
}

bool TBlockRangeField::Remove(TBlockRange16 range)
{
    bool changed = false;
    if (Impl) {
        Y_ABORT_UNLESS(Impl->TryRemove(range, &changed));
        CollapseImpl();
        return changed;
    }

    if (Simple.TryRemove(range, &changed)) {
        return changed;
    }

    Impl = MakeStdSetImpl(MaxBlockCount, Simple, Pool);
    Y_ABORT_UNLESS(Impl->TryRemove(range, &changed));
    Simple.Clear();
    return changed;
}

bool TBlockRangeField::Remove(const TBlockRangeField& field)
{
    if (this == &field) {
        return Clear();
    }

    bool changed = false;
    field.Enumerate(
        [&](TBlockRange16 range)
        {
            changed |= Remove(range);
            return Empty() ? EEnumerateContinuation::Stop
                           : EEnumerateContinuation::Continue;
        });
    return changed;
}

bool TBlockRangeField::Clear()
{
    if (Empty()) {
        return false;
    }

    Simple.Clear();
    Impl.reset();
    Pool.reset();
    return true;
}

bool TBlockRangeField::Overlaps(TBlockRange16 other) const
{
    if (Empty()) {
        return false;
    }
    return Impl ? Impl->Overlaps(other) : Simple.Overlaps(other);
}

bool TBlockRangeField::Overlaps(const TBlockRangeField& other) const
{
    if (Empty()) {
        return false;
    }

    bool overlaps = false;
    other.Enumerate(
        [&](TBlockRange16 range)
        {
            overlaps = Overlaps(range);
            return overlaps ? EEnumerateContinuation::Stop
                            : EEnumerateContinuation::Continue;
        });
    return overlaps;
}

void TBlockRangeField::Enumerate(TEnumerateFunc func) const
{
    (Impl ? Impl.get() : &Simple)->Enumerate(std::move(func));
}

bool TBlockRangeField::Empty() const
{
    return !Impl && Simple.Empty();
}

size_t TBlockRangeField::GetBlockCount() const
{
    return Impl ? Impl->GetBlockCount() : Simple.GetBlockCount();
}

size_t TBlockRangeField::GetSegmentCount() const
{
    size_t segmentCount = 0;
    Enumerate(
        [&](TBlockRange16)
        {
            ++segmentCount;
            return EEnumerateContinuation::Continue;
        });
    return segmentCount;
}

TString TBlockRangeField::Print() const
{
    return Impl ? Impl->Print() : Simple.Print();
}

void TBlockRangeField::CollapseImpl()
{
    if (!Impl) {
        return;
    }
    if (Impl->Empty()) {
        Impl.reset();
        Pool.reset();
    }
}

size_t TBlockRangeField::GetUsedBytes() const
{
    return Pool ? Pool->GetUsedBytes() : 0;
}

size_t TBlockRangeField::GetPoolSize() const
{
    return Pool ? Pool->GetPoolSize() : 0;
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
