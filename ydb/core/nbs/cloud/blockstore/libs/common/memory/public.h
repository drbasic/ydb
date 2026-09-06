#pragma once

#include <util/generic/size_literals.h>

#include <memory>

namespace NYdb::NBS::NBlockStore {

////////////////////////////////////////////////////////////////////////////////

class IArenaAllocator;
using IArenaAllocatorPtr = std::shared_ptr<IArenaAllocator>;

}   // namespace NYdb::NBS::NBlockStore
