#include "write_request.h"

#include "direct_block_group.h"
#include "write_with_direct_replication_request.h"
#include "write_with_pb_replication_request.h"

#include <ydb/core/nbs/cloud/blockstore/libs/common/constants.h>
#include <ydb/core/nbs/cloud/blockstore/libs/diagnostics/trace_helpers.h>
#include <ydb/core/nbs/cloud/blockstore/libs/storage/partition_direct/model/oracle.h>

#include <ydb/library/actors/core/log.h>
#include <ydb/library/services/services.pb.h>

namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect {

////////////////////////////////////////////////////////////////////////////////

TBaseWriteRequestExecutor::TBaseWriteRequestExecutor(
    NActors::TActorSystem* actorSystem,
    TChildLogTitle logTitle,
    const TVChunkConfig& vChunkConfig,
    IDirectBlockGroupPtr directBlockGroup,
    TWriteRequestBundle* bundle)
    : ActorSystem(actorSystem)
    , LogTitle(std::move(logTitle))
    , VChunkConfig(vChunkConfig)
    , DirectBlockGroup(std::move(directBlockGroup))
    , Bundle(bundle)
    , HedgingDelay(DirectBlockGroup->GetOracle()->GetWriteHedgingDelay())
    , RequestTimeout(DirectBlockGroup->GetOracle()->GetWriteRequestTimeout())
{}

TBaseWriteRequestExecutor::~TBaseWriteRequestExecutor()
{
    if (!IsAlreadyReplied()) {
        LOG_ERROR(
            *ActorSystem,
            NKikimrServices::NBS_PARTITION,
            "%s Reply not sent.",
            LogTitle.GetWithTime().c_str());

        Y_ABORT_UNLESS(false);
    }
}

void TBaseWriteRequestExecutor::Run()
{
    Bundle->GetSpan().Event("Run");
    ScheduleRequestTimeoutCallback();
    ScheduleHedging();
}

void TBaseWriteRequestExecutor::OnWriteToPBufferResponse(
    THostIndex host,
    const TDBGWriteBlocksResponse& response)
{
    LOG_DEBUG(
        *ActorSystem,
        NKikimrServices::NBS_PARTITION,
        "%s OnWriteToPBufferResponse. %s %s",
        LogTitle.GetWithTime().c_str(),
        PrintHostIndex(host).c_str(),
        FormatError(response.Error).c_str());

    if (!HasError(response.Error)) {
        if (ShouldReplyOk(THostMask::MakeOne(host))) {
            ReplyOrNotifyBelated(MakeError(S_OK), THostMask::MakeOne(host));
        }
        Bundle->GetHostSpan(host).EndOk();
        return;
    }

    const auto candidates =
        VChunkConfig.GetSecondaryPBuffers().Exclude(RequestedWrites);
    if (auto next = candidates.First()) {
        LOG_WARN(
            *ActorSystem,
            NKikimrServices::NBS_PARTITION,
            "%s Try hand-off %s due to %s on %s",
            LogTitle.GetWithTime().c_str(),
            PrintHostIndex(*next).c_str(),
            FormatError(response.Error).c_str(),
            PrintHostIndex(host).c_str());

        SendWriteRequest(*next);
    } else {
        LOG_ERROR(
            *ActorSystem,
            NKikimrServices::NBS_PARTITION,
            "%s All hand-offs attempts are over. %s",
            LogTitle.GetWithTime().c_str(),
            FormatError(response.Error).c_str());

        ReplyOrNotifyBelated(response.Error, {});
    }

    Bundle->GetHostSpan(host).EndError(FormatError(response.Error));
}

TString TBaseWriteRequestExecutor::ExtendedDebugState() const
{
    TStringBuilder result;
    result << "RequestedWrites: " << RequestedWrites.Print() << ";";
    result << "CompletedWrites: " << CompletedWrites.Print() << ";";

    return result;
}

void TBaseWriteRequestExecutor::ReplyOrNotifyBelated(
    NProto::TError error,
    THostMask completedOnCurrentResponse)
{
    if (!IsReplied) {
        Reply(std::move(error));
        return;
    }
    NotifyBelated(completedOnCurrentResponse);
}

void TBaseWriteRequestExecutor::Reply(NProto::TError error)
{
    Y_ABORT_IF(IsReplied, "TBaseWriteRequestExecutor::Reply called twice");
    IsReplied = true;

    if (HasError(error)) {
        LOG_ERROR(
            *ActorSystem,
            NKikimrServices::NBS_PARTITION,
            "%s Reply error %s",
            LogTitle.GetWithTime().c_str(),
            FormatError(error).c_str());
    } else {
        LOG_DEBUG(
            *ActorSystem,
            NKikimrServices::NBS_PARTITION,
            "%s Reply OK.",
            LogTitle.GetWithTime().c_str());
    }

    Bundle->Reply(std::move(error), RequestedWrites, CompletedWrites);
}

void TBaseWriteRequestExecutor::NotifyBelated(
    THostMask completedOnCurrentResponse)
{
    LOG_DEBUG(
        *ActorSystem,
        NKikimrServices::NBS_PARTITION,
        "%s NotifyBelated",
        LogTitle.GetWithTime().c_str());

    if (!completedOnCurrentResponse.Empty()) {
        Bundle->NotifyBelated(completedOnCurrentResponse);
    }
}

void TBaseWriteRequestExecutor::SendWriteRequest(THostIndex host)
{
    if (IsAlreadyReplied()) {
        return;
    }

    LOG_DEBUG(
        *ActorSystem,
        NKikimrServices::NBS_PARTITION,
        "%s SendWriteRequest. HostIndex: %s",
        LogTitle.GetWithTime().c_str(),
        PrintHostIndex(host).c_str());

    auto& span = Bundle->GetHostSpan(host);
    span.Attribute("HostIndex", static_cast<i64>(host));

    RequestedWrites.Set(host);

    DirectBlockGroup->WriteBlocksToPBuffer(Bundle->AsShared(), host);
}

void TBaseWriteRequestExecutor::ScheduleRequestTimeoutCallback()
{
    if (!RequestTimeout) {
        return;
    }

    LOG_DEBUG(
        *ActorSystem,
        NKikimrServices::NBS_PARTITION,
        "%s ScheduleRequestTimeoutCallback",
        LogTitle.GetWithTime().c_str());

    DirectBlockGroup->Schedule(
        RequestTimeout,
        [weakSelf = weak_from_this()]()
        {
            if (auto self = weakSelf.lock()) {
                self->RequestTimeoutCallback();
            }
        });
}

void TBaseWriteRequestExecutor::RequestTimeoutCallback()
{
    LOG_WARN(
        *ActorSystem,
        NKikimrServices::NBS_PARTITION,
        "%s Request timeout.",
        LogTitle.GetWithTime().c_str());

    ReplyOrNotifyBelated(MakeError(E_TIMEOUT, "Write request timeout"), {});
}

bool TBaseWriteRequestExecutor::IsAlreadyReplied() const
{
    return IsReplied;
}

bool TBaseWriteRequestExecutor::ShouldReplyOk(THostMask newCompletions)
{
    CompletedWrites = CompletedWrites.Include(newCompletions);
    return CompletedWrites.Count() >= QuorumDirectBlockGroupHostCount;
}

TVector<THostIndex> TBaseWriteRequestExecutor::GetAvailableHandOffHosts() const
{
    return VChunkConfig.GetSecondaryPBuffers().Exclude(RequestedWrites).Hosts();
}

////////////////////////////////////////////////////////////////////////////////

TBaseWriteRequestExecutorPtr CreateWriteRequestExecutor(
    NActors::TActorSystem* const actorSystem,
    const TLogTitle& logTitle,
    const TVChunkConfig& vChunkConfig,
    IDirectBlockGroupPtr directBlockGroup,
    TWriteRequestBundle* bundle)
{
    TBaseWriteRequestExecutorPtr result;
    switch (directBlockGroup->GetOracle()->GetWriteMode()) {
        case EWriteMode::PBufferReplication:
            result = std::make_shared<TWriteWithPbReplicationRequestExecutor>(
                actorSystem,
                logTitle.GetChildWithTags(
                    GetCycleCount(),
                    {{"t", "p-write"},
                     {"r", bundle->GetVChunkRange().Print()}}),
                vChunkConfig,
                std::move(directBlockGroup),
                bundle);
            break;
        case EWriteMode::DirectPBuffersFilling:
            result =
                std::make_shared<TWriteWithDirectReplicationRequestExecutor>(
                    actorSystem,
                    logTitle.GetChildWithTags(
                        GetCycleCount(),
                        {{"t", "d-write"},
                         {"r", bundle->GetVChunkRange().Print()}}),
                    vChunkConfig,
                    std::move(directBlockGroup),
                    bundle);
            break;
    }
    bundle->AttachExecutor(result);
    return result;
}

}   // namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect
