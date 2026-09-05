#pragma once

#include <cstddef>

namespace NYdb::NBS::NBlockStore {

//////////////////////////////////////////////////////////////////////////////

class IArenaAllocator
{
public:
    virtual ~IArenaAllocator() = default;

    virtual void* Allocate(size_t size) = 0;
    virtual void DeAllocate(void* ptr) = 0;

    [[nodiscard]] virtual size_t AllocatedBlocks() const = 0;
    [[nodiscard]] virtual size_t AllocatedSize() const = 0;
};

//////////////////////////////////////////////////////////////////////////////

IArenaAllocator* CreateArenaAllocator();

//////////////////////////////////////////////////////////////////////////////
}   // namespace NYdb::NBS::NBlockStore
