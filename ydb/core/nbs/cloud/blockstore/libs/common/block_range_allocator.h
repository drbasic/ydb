#pragma once

#include <util/generic/yexception.h>

#include <cstddef>
#include <new>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

// Lightweight bump allocator backed by a single heap-allocated block.
// Memory is never freed until the pool is destroyed.
class TBlockRangePool
{
public:
    explicit TBlockRangePool(size_t poolSizeBytes)
        : PoolSize_(poolSizeBytes)
        , Pool_(static_cast<char*>(::operator new(poolSizeBytes)))
        , Used_(0)
    {}

    ~TBlockRangePool()
    {
        ::operator delete(Pool_);
    }

    // Non-copyable, non-movable (ownership semantics)
    TBlockRangePool(const TBlockRangePool&) = delete;
    TBlockRangePool& operator=(const TBlockRangePool&) = delete;
    TBlockRangePool(TBlockRangePool&&) = delete;
    TBlockRangePool& operator=(TBlockRangePool&&) = delete;

    void* Allocate(size_t size)
    {
        if (Used_ + size > PoolSize_) {
            ythrow yexception()
                << "TBlockRangePool exhausted: requested " << size
                << " bytes, but only " << (PoolSize_ - Used_)
                << " bytes remaining (of " << PoolSize_ << " total)";
        }

        void* ptr = Pool_ + Used_;
        Used_ += size;
        return ptr;
    }

    void Deallocate(void*, size_t) noexcept
    {}

    size_t GetUsedBytes() const noexcept
    {
        return Used_;
    }

    size_t GetPoolSize() const noexcept
    {
        return PoolSize_;
    }

    double GetUsagePercent() const noexcept
    {
        if (!PoolSize_) {
            return 0;
        }
        return static_cast<double>(Used_) / static_cast<double>(PoolSize_) *
               100.0;
    }

private:
    const size_t PoolSize_;
    char* const Pool_;
    size_t Used_ = 0;
};

//////////////////////////////////////////////////////////////////////////////

// Allocator that references a per-instance TBlockRangePool (raw pointer).
// The pool is owned by TBlockRangeField::TImpl and outlives the set.
template <class T>
class TBlockRangeFieldAllocator
{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    explicit TBlockRangeFieldAllocator(TBlockRangePool* pool) noexcept
        : Pool_(pool)
    {}

    template <class U>
    TBlockRangeFieldAllocator(
        const TBlockRangeFieldAllocator<U>& other) noexcept
        : Pool_(other.Pool_)
    {}

    pointer allocate(size_type n)
    {
        Y_ENSURE(Pool_, "TBlockRangeFieldAllocator: pool is not set");
        return static_cast<pointer>(Pool_->Allocate(n * sizeof(T)));
    }

    void deallocate(pointer /*ptr*/, size_type /*n*/) noexcept
    {}

    template <class U>
    struct rebind
    {
        using other = TBlockRangeFieldAllocator<U>;
    };

    TBlockRangePool* GetPool() const noexcept
    {
        return Pool_;
    }

private:
    TBlockRangePool* Pool_;

    template <class U>
    friend class TBlockRangeFieldAllocator;
};

template <class T1, class T2>
inline bool operator==(
    const TBlockRangeFieldAllocator<T1>& l,
    const TBlockRangeFieldAllocator<T2>& r) noexcept
{
    return l.GetPool() == r.GetPool();
}

template <class T1, class T2>
inline bool operator!=(
    const TBlockRangeFieldAllocator<T1>& l,
    const TBlockRangeFieldAllocator<T2>& r) noexcept
{
    return !(l == r);
}

}   // namespace NYdb::NBS::NBlockStore
