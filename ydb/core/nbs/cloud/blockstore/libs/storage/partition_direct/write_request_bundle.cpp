
#include "write_request_bundle.h"

#include <ydb/core/nbs/cloud/blockstore/libs/service/context.h>

namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect {

////////////////////////////////////////////////////////////////////////////////

TWriteRequestBundle::TWriteRequestBundle(
    NActors::TActorSystem* const actorSystem,
    IWriteClientWeakPtr writeClient,
    std::shared_ptr<TWriteBlocksLocalRequest> request,
    const NWilson::TTraceId& traceId,
    TCallContextPtr callContext,
    ui32 vchunkIndex,
    TBlockRange64 vchunkRange)
    : WriteClient(std::move(writeClient))
    , Request(std::move(request))
    , Span(
          NKikimr::TWilsonNbs::NbsBasic,
          traceId.Clone(),
          "TVChunk.Write",
          NWilson::EFlags::AUTO_END,
          actorSystem)
    , CallContext(std::move(callContext))
    , VChunkIndex(vchunkIndex)
    , VChunkRange(vchunkRange)
    , Promise(NThreading::NewPromise<TWriteBlocksLocalResponse>())
{}

void TWriteRequestBundle::AttachExecutor(
    IWriteRequestExecutorPtr writeRequestExecutor)
{
    WriteRequestExecutor = std::move(writeRequestExecutor);
}

void TWriteRequestBundle::Reply(
    NProto::TError error,
    THostMask requestedWrites,
    THostMask completedWrites)
{
    Request->Sglist.Close();

    if (auto client = WriteClient.lock()) {
        client->OnWriteBlocksResponse(
            shared_from_this(),
            TWriteRequestResponse{
                .Error = std::move(error),
                .Lsn = Lsn,
                .RequestedWrites = requestedWrites,
                .CompletedWrites = completedWrites});
    } else {
        SendFinalReply(
            TWriteBlocksLocalResponse{.Error = MakeError(E_CANCELLED)});
    }
}

void TWriteRequestBundle::NotifyBelated(THostMask hosts)
{
    if (auto client = WriteClient.lock()) {
        client->OnBelatedWriteBlocksResponse(shared_from_this(), hosts);
    }
}

void TWriteRequestBundle::SendFinalReply(TWriteBlocksLocalResponse response)
{
    auto span = Span.CreateChild(
        NKikimr::TWilsonNbs::NbsBasic,
        "Reply",
        NWilson::EFlags::AUTO_END);
    Promise.SetValue(std::move(response));
}

void TWriteRequestBundle::WriteToPBufferResult(
    THostIndex host,
    const TDBGWriteBlocksResponse& response)
{
    Y_ABORT_UNLESS(WriteRequestExecutor);

    WriteRequestExecutor->OnWriteToPBufferResponse(host, response);
}

NThreading::TFuture<TWriteBlocksLocalResponse> TWriteRequestBundle::GetFuture()
{
    return Promise.GetFuture();
}

NWilson::TSpan& TWriteRequestBundle::GetSpan()
{
    return Span;
}

NWilson::TSpan& TWriteRequestBundle::GetHostSpan(THostIndex host)
{
    if (Span && !HostSpans[host]) {
        HostSpans[host] = Span.CreateChild(
            NKikimr::TWilsonNbs::NbsBasic,
            "WriteToPBuffer",
            NWilson::EFlags::AUTO_END);
    }
    return HostSpans[host];
}

ui32 TWriteRequestBundle::GetVChunkIndex() const
{
    return VChunkIndex;
}

TBlockRange64 TWriteRequestBundle::GetVChunkRange() const
{
    return VChunkRange;
}

void TWriteRequestBundle::SetLsn(ui64 lsn)
{
    Lsn = lsn;
}

ui64 TWriteRequestBundle::GetLsn() const
{
    return Lsn;
}

TGuardedSgList& TWriteRequestBundle::GetSgList()
{
    return Request->Sglist;
}

TWriteRequestBundlePtr TWriteRequestBundle::AsShared()
{
    return shared_from_this();
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace NYdb::NBS::NBlockStore::NStorage::NPartitionDirect
