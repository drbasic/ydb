#pragma once

#include "arena_allocator_pool.h"

#include <ydb/core/nbs/cloud/storage/core/libs/common/disable_copy.h>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// Allocator that references a TBlockRangePool (raw pointer).
// The pool is owned by TBlockRangeField and outlives the set.
template <class T>
class TArenaPoolAdapter
{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    explicit TArenaPoolAdapter(TArenaAllocatorPool* pool) noexcept
        : Pool_(pool)
    {}

    template <class U>
    TArenaPoolAdapter(const TArenaPoolAdapter<U>& other) noexcept
        : Pool_(other.Pool_)
    {}

    pointer allocate(size_type n)
    {
        return static_cast<pointer>(Pool_->Allocate(n * sizeof(T)));
    }

    void deallocate(pointer ptr, size_type n) noexcept
    {
        Y_UNUSED(n);
        Pool_->Deallocate(ptr);
    }

    template <class U>
    struct rebind
    {
        using other = TArenaPoolAdapter<U>;
    };

    TArenaAllocatorPool* GetPool() const noexcept
    {
        return Pool_;
    }

private:
    TArenaAllocatorPool* Pool_;

    template <class U>
    friend class TArenaPoolAdapter;
};

/////////////////////////////////////////////////////////////////////////////

template <class T1, class T2>
inline bool operator==(
    const TArenaPoolAdapter<T1>& l,
    const TArenaPoolAdapter<T2>& r) noexcept
{
    return l.GetPool() == r.GetPool();
}

template <class T1, class T2>
inline bool operator!=(
    const TArenaPoolAdapter<T1>& l,
    const TArenaPoolAdapter<T2>& r) noexcept
{
    return !(l == r);
}

/////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore
