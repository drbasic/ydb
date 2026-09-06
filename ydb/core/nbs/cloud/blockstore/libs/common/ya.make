LIBRARY()

GENERATE_ENUM_SERIALIZATION(block_range_field_impl.h)

SRCS(
    block_range_algorithms.cpp
    block_range_field.cpp
    block_range_field_flat_set.cpp
    block_range_field_impl.cpp
    block_range_field_set.cpp
    block_range_field_simple.cpp
    block_range_field_std_set.cpp
    block_range_map.cpp
    block_range.cpp
    printable_params.cpp
    pbuffer_key.cpp
    thread_checker.cpp
)

PEERDIR(
    ydb/core/nbs/cloud/blockstore/libs/common/memory
    ydb/core/nbs/cloud/storage/core/libs/coroutine
    library/cpp/lwtrace
    util
)

END()

RECURSE_FOR_TESTS(
    benchmark
    memory
    ut
)
