#pragma once

#include <util/generic/utility.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

#include <cstddef>
#include <new>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////

// Default size of the first chunk allocated by TBlockRangePool.
inline constexpr size_t DefaultBlockRangePoolChunkSize = 4 * 1024;

////////////////////////////////////////////////////////////////////////////

// Lightweight bump allocator backed by heap-allocated chunks.
// Individual allocations are never freed, but when the current chunk is
// exhausted the pool grows by allocating a new (larger) chunk, so it never
// overflows. Previously allocated chunks stay alive, keeping all issued
// pointers valid for the pool lifetime.
class TBlockRangePool
{
public:
    explicit TBlockRangePool(size_t chunkSizeBytes)
        : ChunkSize_(Max<size_t>(chunkSizeBytes, MinChunkSize))
    {}

    ~TBlockRangePool()
    {
        for (const auto& chunk: Chunks_) {
            ::operator delete(chunk.Memory);
        }
    }

    // Non-copyable, non-movable (ownership semantics)
    TBlockRangePool(const TBlockRangePool&) = delete;
    TBlockRangePool& operator=(const TBlockRangePool&) = delete;
    TBlockRangePool(TBlockRangePool&&) = delete;
    TBlockRangePool& operator=(TBlockRangePool&&) = delete;

    void* Allocate(size_t size)
    {
        if (Chunks_.empty() ||
            UsedInLastChunk_ + size > Chunks_.back().Capacity)
        {
            AddChunk(size);
        }

        auto& chunk = Chunks_.back();
        void* ptr = chunk.Memory + UsedInLastChunk_;
        UsedInLastChunk_ += size;
        UsedBytes_ += size;
        return ptr;
    }

    void Deallocate(void*, size_t) noexcept
    {}

    size_t GetUsedBytes() const noexcept
    {
        return UsedBytes_;
    }

    size_t GetPoolSize() const noexcept
    {
        return CapacityBytes_;
    }

    double GetUsagePercent() const noexcept
    {
        if (!CapacityBytes_) {
            return 0;
        }
        return static_cast<double>(UsedBytes_) /
               static_cast<double>(CapacityBytes_) * 100.0;
    }

private:
    struct TChunk
    {
        char* Memory = nullptr;
        size_t Capacity = 0;
    };

    void AddChunk(size_t size)
    {
        size_t capacity = ChunkSize_;
        if (!Chunks_.empty()) {
            capacity = Min<size_t>(Chunks_.back().Capacity * 2, MaxChunkSize);
        }
        capacity = Max<size_t>(capacity, size);

        auto* memory = static_cast<char*>(::operator new(capacity));
        Chunks_.push_back(TChunk{memory, capacity});
        CapacityBytes_ += capacity;
        UsedInLastChunk_ = 0;
    }

    static constexpr size_t MinChunkSize = 256;
    static constexpr size_t MaxChunkSize = 1 << 20;

    const size_t ChunkSize_;
    TVector<TChunk> Chunks_;
    size_t UsedInLastChunk_ = 0;
    size_t UsedBytes_ = 0;
    size_t CapacityBytes_ = 0;
};

////////////////////////////////////////////////////////////////////////////

// Allocator that references a TBlockRangePool (raw pointer).
// The pool is owned by TBlockRangeField and outlives the set.
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
