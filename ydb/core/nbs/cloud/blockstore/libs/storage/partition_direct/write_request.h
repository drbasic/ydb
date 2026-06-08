#pragma once

#include "public.h"

#include "write_request_bundle.h"

#include <ydb/core/nbs/cloud/blockstore/config/config.h>
#include <ydb/core/nbs/cloud/blockstore/config/protos/storage.pb.h>
#include <ydb/core/nbs/cloud/blockstore/libs/service/context.h>
#include <ydb/core/nbs/cloud/blockstore/libs/service/request.h>
#include <ydb/core/nbs/cloud/blockstore/libs/storage/model/log_title.h>
#include <ydb/core/nbs/cloud/blockstore/libs/storage/partition_direct/direct_block_group.h>
#include <ydb/core/nbs/cloud/blockstore/libs/storage/partition_direct/model/host_roles.h>
#include <ydb/core/nbs/cloud/blockstore/libs/storage/partition_direct/model/vchunk_config.h>

#include <ydb/library/actors/core/actorsystem.h>
#include <ydb/library/actors/wilson/wilson_span.h>

#include <functional>

namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect {

////////////////////////////////////////////////////////////////////////////////

class TBaseWriteRequestExecutor
    : public IWriteRequestExecutor
    , public std::enable_shared_from_this<TBaseWriteRequestExecutor>
{
public:
    TBaseWriteRequestExecutor(
        NActors::TActorSystem* actorSystem,
        TChildLogTitle logTitle,
        const TVChunkConfig& vChunkConfig,
        IDirectBlockGroupPtr directBlockGroup,
        TWriteRequestBundle* bundle);

    ~TBaseWriteRequestExecutor() override;

    virtual void Run();

    // IWriteToPBufferClient implementation
    void OnWriteToPBufferResponse(
        THostIndex host,
        const TDBGWriteBlocksResponse& response) override;

protected:
    void ReplyOrNotifyBelated(
        NProto::TError error,
        THostMask completedOnCurrentResponse);
    void Reply(NProto::TError error);
    void NotifyBelated(THostMask completedOnCurrentResponse);

    void SendWriteRequest(THostIndex host);

    void ScheduleRequestTimeoutCallback();
    void RequestTimeoutCallback();

    [[nodiscard]] bool IsAlreadyReplied() const;
    [[nodiscard]] bool ShouldReplyOk(THostMask newCompletions);

    TVector<THostIndex> GetAvailableHandOffHosts() const;
    virtual TString ExtendedDebugState() const;

    virtual void ScheduleHedging() = 0;

    NActors::TActorSystem* ActorSystem;
    const TChildLogTitle LogTitle;
    const TVChunkConfig VChunkConfig;
    const IDirectBlockGroupPtr DirectBlockGroup;
    TWriteRequestBundle* const Bundle;
    const TDuration HedgingDelay;
    const TDuration RequestTimeout;

    THostMask RequestedWrites;
    THostMask CompletedWrites;

private:
    bool IsReplied = false;
};

using TBaseWriteRequestExecutorPtr = std::shared_ptr<TBaseWriteRequestExecutor>;

////////////////////////////////////////////////////////////////////////////////

TBaseWriteRequestExecutorPtr CreateWriteRequestExecutor(
    NActors::TActorSystem* const actorSystem,
    const TLogTitle& logTitle,
    const TVChunkConfig& vChunkConfig,
    IDirectBlockGroupPtr directBlockGroup,
    TWriteRequestBundle* bundle);

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect
