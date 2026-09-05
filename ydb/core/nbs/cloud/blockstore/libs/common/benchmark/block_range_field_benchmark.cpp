#include <ydb/core/nbs/cloud/blockstore/libs/common/block_range_field.h>

#include <util/generic/vector.h>
#include <util/random/fast.h>

#include <benchmark/benchmark.h>

#include <array>
#include <memory>
#include <span>

using namespace NYdb::NBS::NBlockStore;

namespace {

constexpr ui16 BlockCount = 32768;
constexpr ui32 RemoveRangeLength = 256;
constexpr size_t AdjacentRangeBatchSize = 10;

// Backends the benchmarks are run against. Every benchmark below is
// registered once per entry of this array.
static constexpr std::array SupportedBackends = {
    TBlockRangeField::EBackend::StdSet,
    TBlockRangeField::EBackend::Set,
    TBlockRangeField::EBackend::FlatSet,
};

TString BackendName(TBlockRangeField::EBackend backend)
{
    switch (backend) {
        case TBlockRangeField::EBackend::Simple:
            return "Simple";
        case TBlockRangeField::EBackend::StdSet:
            return "StdSet";
        case TBlockRangeField::EBackend::Set:
            return "Set";
        case TBlockRangeField::EBackend::FlatSet:
            return "FlatSet";
        case TBlockRangeField::EBackend::Bitmask:
            return "Bitmask";
    }
    Y_ABORT("Unknown backend");
}

// Describes what percentage of generated ranges has the specified length.
struct TRangeSizeShare
{
    ui16 RangeLength;
    ui32 Percent;
};

constexpr std::array RangeSizeDistribution = {
    TRangeSizeShare{.RangeLength = 1, .Percent = 25},
    TRangeSizeShare{.RangeLength = 2, .Percent = 25},
    TRangeSizeShare{.RangeLength = 4, .Percent = 20},
    TRangeSizeShare{.RangeLength = 8, .Percent = 15},
    TRangeSizeShare{.RangeLength = 16, .Percent = 15},
};

void ValidateDistribution(std::span<const TRangeSizeShare> distribution)
{
    ui32 totalPercent = 0;
    for (const auto& item: distribution) {
        Y_ABORT_UNLESS(item.RangeLength > 0 && item.RangeLength <= BlockCount);
        totalPercent += item.Percent;
    }
    Y_ABORT_UNLESS(totalPercent == 100);
}

ui16 MakeRangeLength(
    TReallyFastRng32& rng,
    std::span<const TRangeSizeShare> distribution)
{
    const ui32 percentile = rng.Uniform(100);
    ui32 cumulativePercent = 0;
    for (const auto& item: distribution) {
        cumulativePercent += item.Percent;
        if (percentile < cumulativePercent) {
            return item.RangeLength;
        }
    }
    Y_ABORT("Invalid range size distribution");
}

TVector<TBlockRange16> MakeInsertRanges(
    size_t rangeCount,
    std::span<const TRangeSizeShare> distribution)
{
    ValidateDistribution(distribution);

    TReallyFastRng32 rng(42);
    TVector<TBlockRange16> ranges(Reserve(rangeCount));
    for (size_t i = 0; i < rangeCount; ++i) {
        const ui16 length = MakeRangeLength(rng, distribution);
        const ui32 maxRangeStart = BlockCount - length;
        const ui16 start = static_cast<ui16>(rng.Uniform(maxRangeStart + 1));
        ranges.push_back(TBlockRange16::WithLength(start, length));
    }
    return ranges;
}

TVector<TBlockRange16> MakeRightAdjacentRanges(
    size_t rangeCount,
    std::span<const TRangeSizeShare> distribution)
{
    ValidateDistribution(distribution);
    Y_ABORT_UNLESS(rangeCount % AdjacentRangeBatchSize == 0);

    TReallyFastRng32 rng(42);
    TVector<TBlockRange16> ranges(Reserve(rangeCount));
    while (ranges.size() < rangeCount) {
        std::array<ui16, AdjacentRangeBatchSize> lengths;
        ui32 batchBlockCount = 0;
        for (auto& length: lengths) {
            length = MakeRangeLength(rng, distribution);
            batchBlockCount += length;
        }
        Y_ABORT_UNLESS(batchBlockCount <= BlockCount);

        ui32 start = rng.Uniform(BlockCount - batchBlockCount + 1);
        for (const ui16 length: lengths) {
            ranges.push_back(
                TBlockRange16::WithLength(static_cast<ui16>(start), length));
            start += length;
        }
    }
    return ranges;
}

TVector<TBlockRange16> MakeRemoveRanges()
{
    TVector<TBlockRange16> ranges(Reserve(BlockCount / RemoveRangeLength));
    for (ui32 start = 0; start < BlockCount; start += RemoveRangeLength) {
        ranges.push_back(TBlockRange16::WithLength(
            static_cast<ui16>(start),
            RemoveRangeLength));
    }
    return ranges;
}

}   // namespace

// Measures inserting a batch of reproducible random ranges. Range generation,
// field construction and destruction are excluded from the timed section.
static void BM_BlockRangeFieldAdd(
    benchmark::State& state,
    TBlockRangeField::EBackend backend)
{
    const auto ranges = MakeInsertRanges(state.range(0), RangeSizeDistribution);

    for (auto _: state) {
        state.PauseTiming();
        auto field = std::make_unique<TBlockRangeField>(BlockCount, backend);
        state.ResumeTiming();

        for (const auto& range: ranges) {
            field->Add(range);
        }

        state.PauseTiming();
        field.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * ranges.size());
}

// Measures batches where one range is placed randomly and the next nine
// ranges are added immediately to its right.
static void BM_BlockRangeFieldAddRightAdjacent(
    benchmark::State& state,
    TBlockRangeField::EBackend backend)
{
    const auto ranges =
        MakeRightAdjacentRanges(state.range(0), RangeSizeDistribution);

    for (auto _: state) {
        state.PauseTiming();
        auto field = std::make_unique<TBlockRangeField>(BlockCount, backend);
        state.ResumeTiming();

        for (const auto& range: ranges) {
            field->Add(range);
        }

        state.PauseTiming();
        field.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * ranges.size());
}

// Measures adding consecutive blocks one at a time. Field construction and
// destruction are excluded from the timed section.
static void BM_BlockRangeFieldAddSequentialBlocks(
    benchmark::State& state,
    TBlockRangeField::EBackend backend)
{
    const auto blockCount = static_cast<ui16>(state.range(0));
    Y_ABORT_UNLESS(blockCount <= BlockCount);

    for (auto _: state) {
        state.PauseTiming();
        auto field = std::make_unique<TBlockRangeField>(BlockCount, backend);
        state.ResumeTiming();

        for (ui16 blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
            field->Add(TBlockRange16::MakeOneBlock(blockIndex));
        }

        state.PauseTiming();
        field.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * blockCount);
}

// Measures removing the whole block space in consecutive 256-block ranges.
// Random field population and destruction are excluded from the timed section.
static void BM_BlockRangeFieldRemove(
    benchmark::State& state,
    TBlockRangeField::EBackend backend)
{
    const auto ranges = MakeInsertRanges(state.range(0), RangeSizeDistribution);
    const auto removeRanges = MakeRemoveRanges();

    for (auto _: state) {
        state.PauseTiming();
        auto field = std::make_unique<TBlockRangeField>(BlockCount, backend);
        for (const auto& range: ranges) {
            field->Add(range);
        }
        state.ResumeTiming();

        for (const auto& range: removeRanges) {
            field->Remove(range);
        }

        state.PauseTiming();
        field.reset();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * removeRanges.size());
}

namespace {

// Registers every benchmark once per backend from SupportedBackends. Runs
// during static initialization, before main(), just like the BENCHMARK()
// macro does.
const auto RegisteredBenchmarks = []()
{
    for (const auto backend: SupportedBackends) {
        const TString suffix = "/" + BackendName(backend);

        benchmark::RegisterBenchmark(
            ("BM_BlockRangeFieldAdd" + suffix).c_str(),
            BM_BlockRangeFieldAdd,
            backend)
            ->Arg(1000)
            ->Arg(2000)
            ->Arg(5000)
            ->Unit(benchmark::kMicrosecond);

        benchmark::RegisterBenchmark(
            ("BM_BlockRangeFieldAddRightAdjacent" + suffix).c_str(),
            BM_BlockRangeFieldAddRightAdjacent,
            backend)
            ->Arg(1000)
            ->Arg(2000)
            ->Arg(5000)
            ->Unit(benchmark::kMicrosecond);

        benchmark::RegisterBenchmark(
            ("BM_BlockRangeFieldAddSequentialBlocks" + suffix).c_str(),
            BM_BlockRangeFieldAddSequentialBlocks,
            backend)
            ->Arg(32768)
            ->Unit(benchmark::kMicrosecond);

        benchmark::RegisterBenchmark(
            ("BM_BlockRangeFieldRemove" + suffix).c_str(),
            BM_BlockRangeFieldRemove,
            backend)
            ->Arg(0)
            ->Arg(1000)
            ->Arg(2000)
            ->Arg(5000)
            ->Unit(benchmark::kMicrosecond);
    }
    return 0;
}();

}   // namespace
