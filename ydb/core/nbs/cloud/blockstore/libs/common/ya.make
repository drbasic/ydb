LIBRARY()

SRCS(
    arena_allocator.cpp
    arena_allocator_index_pool.cpp
    arena_allocator_pool.cpp
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
    ydb/core/nbs/cloud/storage/core/libs/coroutine
    library/cpp/lwtrace
    util
)

END()

RECURSE_FOR_TESTS(
    benchmark
    ut
)
