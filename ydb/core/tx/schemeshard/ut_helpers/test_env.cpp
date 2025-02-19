#include "test_env.h"
#include "helpers.h"

#include <ydb/core/blockstore/core/blockstore.h>
#include <ydb/core/base/tablet_resolver.h>
#include <ydb/core/metering/metering.h>
#include <ydb/core/tablet_flat/tablet_flat_executed.h>
#include <ydb/core/tx/datashard/datashard.h>
#include <ydb/core/tx/schemeshard/schemeshard_private.h>
#include <ydb/core/tx/tx_allocator/txallocator.h>
#include <ydb/core/tx/tx_proxy/proxy.h>
#include <ydb/core/filestore/core/filestore.h>

#include <library/cpp/testing/unittest/registar.h>


static const bool ENABLE_SCHEMESHARD_LOG = true;
static const bool ENABLE_DATASHARD_LOG = false;
static const bool ENABLE_COORDINATOR_MEDIATOR_LOG = false;
static const bool ENABLE_SCHEMEBOARD_LOG = false;
static const bool ENABLE_OLAP_LOG = false;
static const bool ENABLE_EXPORT_LOG = false;

using namespace NKikimr;
using namespace NSchemeShard;

// BlockStoreVolume mock for testing schemeshard
class TFakeBlockStoreVolume : public TActor<TFakeBlockStoreVolume>, public NTabletFlatExecutor::TTabletExecutedFlat {
public:
    TFakeBlockStoreVolume(const TActorId& tablet, TTabletStorageInfo* info)
        : TActor(&TThis::StateInit)
          , TTabletExecutedFlat(info, tablet,  new NMiniKQL::TMiniKQLFactory)
    {}

    void OnActivateExecutor(const TActorContext& ctx) override {
        Become(&TThis::StateWork);

        while (!InitialEventsQueue.empty()) {
            TAutoPtr<IEventHandle>& ev = InitialEventsQueue.front();
            ctx.ExecutorThread.Send(ev.Release());
            InitialEventsQueue.pop_front();
        }
    }

    void OnDetach(const TActorContext& ctx) override {
        Die(ctx);
    }

    void OnTabletDead(TEvTablet::TEvTabletDead::TPtr& ev, const TActorContext& ctx) override {
        Y_UNUSED(ev);
        Die(ctx);
    }

    void Enqueue(STFUNC_SIG) override {
        Y_UNUSED(ctx);
        InitialEventsQueue.push_back(ev);
    }

    STFUNC(StateInit) {
        StateInitImpl(ev, ctx);
    }

    STFUNC(StateWork) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTablet::TEvTabletDead, HandleTabletDead);
            HFunc(TEvBlockStore::TEvUpdateVolumeConfig, Handle);
            HFunc(TEvents::TEvPoisonPill, Handle);
        }
    }

    STFUNC(StateBroken) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTablet::TEvTabletDead, HandleTabletDead);
        }
    }

private:
    void Handle(TEvBlockStore::TEvUpdateVolumeConfig::TPtr& ev, const TActorContext& ctx) {
        const auto& request = ev->Get()->Record;
        TAutoPtr<TEvBlockStore::TEvUpdateVolumeConfigResponse> response =
            new TEvBlockStore::TEvUpdateVolumeConfigResponse();
        response->Record.SetTxId(request.GetTxId());
        response->Record.SetOrigin(TabletID());
        response->Record.SetStatus(NKikimrBlockStore::OK);
        ctx.Send(ev->Sender, response.Release());
    }

    void Handle(TEvents::TEvPoisonPill::TPtr& ev, const TActorContext& ctx) {
        Y_UNUSED(ev);
        Become(&TThis::StateBroken);
        ctx.Send(Tablet(), new TEvents::TEvPoisonPill());
    }

private:
    TDeque<TAutoPtr<IEventHandle>> InitialEventsQueue;
};

class TFakeFileStore : public TActor<TFakeFileStore>, public NTabletFlatExecutor::TTabletExecutedFlat {
public:
    TFakeFileStore(const TActorId& tablet, TTabletStorageInfo* info)
        : TActor(&TThis::StateInit)
        , TTabletExecutedFlat(info, tablet,  new NMiniKQL::TMiniKQLFactory)
    {}

    void OnActivateExecutor(const TActorContext& ctx) override {
        Become(&TThis::StateWork);

        while (!InitialEventsQueue.empty()) {
            TAutoPtr<IEventHandle>& ev = InitialEventsQueue.front();
            ctx.ExecutorThread.Send(ev.Release());
            InitialEventsQueue.pop_front();
        }
    }

    void OnDetach(const TActorContext& ctx) override {
        Die(ctx);
    }

    void OnTabletDead(TEvTablet::TEvTabletDead::TPtr& ev, const TActorContext& ctx) override {
        Y_UNUSED(ev);
        Die(ctx);
    }

    void Enqueue(STFUNC_SIG) override {
        Y_UNUSED(ctx);
        InitialEventsQueue.push_back(ev);
    }

    STFUNC(StateInit) {
        StateInitImpl(ev, ctx);
    }

    STFUNC(StateWork) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTablet::TEvTabletDead, HandleTabletDead);
            HFunc(TEvFileStore::TEvUpdateConfig, Handle);
            HFunc(TEvents::TEvPoisonPill, Handle);
        }
    }

    STFUNC(StateBroken) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTablet::TEvTabletDead, HandleTabletDead);
        }
    }

private:
    void Handle(TEvFileStore::TEvUpdateConfig::TPtr& ev, const TActorContext& ctx) {
        const auto& request = ev->Get()->Record;
        TAutoPtr<TEvFileStore::TEvUpdateConfigResponse> response =
            new TEvFileStore::TEvUpdateConfigResponse();
        response->Record.SetTxId(request.GetTxId());
        response->Record.SetOrigin(TabletID());
        response->Record.SetStatus(NKikimrFileStore::OK);
        ctx.Send(ev->Sender, response.Release());
    }

    void Handle(TEvents::TEvPoisonPill::TPtr& ev, const TActorContext& ctx) {
        Y_UNUSED(ev);
        Become(&TThis::StateBroken);
        ctx.Send(Tablet(), new TEvents::TEvPoisonPill());
    }

private:
    TDeque<TAutoPtr<IEventHandle>> InitialEventsQueue;
};

// Automatically resend notification requests to Schemeshard if it gets restarted
class TTxNotificationSubscriber : public TActor<TTxNotificationSubscriber> {
public:
    explicit TTxNotificationSubscriber(ui64 schemeshardTabletId)
        : TActor<TTxNotificationSubscriber>(&TTxNotificationSubscriber::StateWork)
        , SchemeshardTabletId(schemeshardTabletId)
    {}

private:
    void StateWork(TAutoPtr<NActors::IEventHandle> &ev, const NActors::TActorContext &ctx) {
        switch (ev->GetTypeRewrite()) {
        HFunc(TEvTabletPipe::TEvClientConnected, Handle);
        HFunc(TEvTabletPipe::TEvClientDestroyed, Handle);
        HFunc(TEvSchemeShard::TEvNotifyTxCompletion, Handle);
        HFunc(TEvSchemeShard::TEvNotifyTxCompletionResult, Handle);
        };
    }

    void Handle(TEvTabletPipe::TEvClientConnected::TPtr &ev, const TActorContext &ctx) {
        TEvTabletPipe::TEvClientConnected *msg = ev->Get();
        if (msg->Status == NKikimrProto::OK)
            return;

        DoHandleDisconnect(ev->Get()->ClientId, ctx);
    }

    void Handle(TEvTabletPipe::TEvClientDestroyed::TPtr &ev, const TActorContext &ctx) {
        DoHandleDisconnect(ev->Get()->ClientId, ctx);
    }

    void DoHandleDisconnect(TActorId pipeClient, const TActorContext &ctx) {
        if (pipeClient == SchemeShardPipe) {
            SchemeShardPipe = TActorId();
            // Resend all
            for (const auto& w : SchemeTxWaiters) {
                SendToSchemeshard(w.first, ctx);
            }
        }
    }

    void Handle(TEvSchemeShard::TEvNotifyTxCompletion::TPtr &ev, const TActorContext &ctx) {
        ui64 txId = ev->Get()->Record.GetTxId();
        LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                    "tests -- TTxNotificationSubscriber got TEvNotifyTxCompletion"
                    << ", txId: " << txId);

        if (SchemeTxWaiters.contains(txId)) {
            return;
        }

        // Save TxId, forward to schemeshard
        SchemeTxWaiters[txId].insert(ev->Sender);
        SendToSchemeshard(txId, ctx);
    }

    void Handle(TEvSchemeShard::TEvNotifyTxCompletionResult::TPtr &ev, const TActorContext &ctx) {
        ui64 txId = ev->Get()->Record.GetTxId();

        LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                    "tests -- TTxNotificationSubscriber got TEvNotifyTxCompletionResult"
                    << ", txId: " << txId);

        if (!SchemeTxWaiters.contains(txId))
            return;

        // Notifify all waiters and forget TxId
        for (TActorId waiter : SchemeTxWaiters[txId]) {
            LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                        "tests -- TTxNotificationSubscriber satisfy subsriber"
                        << ", waiter: " << waiter
                        << ", txId: " << txId);
            ctx.Send(waiter, new TEvSchemeShard::TEvNotifyTxCompletionResult(txId));
        }
        SchemeTxWaiters.erase(txId);
    }

    void SendToSchemeshard(ui64 txId, const TActorContext &ctx) {
        if (!SchemeShardPipe) {
            SchemeShardPipe = ctx.Register(NTabletPipe::CreateClient(ctx.SelfID, SchemeshardTabletId, GetPipeConfigWithRetries()));
        }
        NTabletPipe::SendData(ctx, SchemeShardPipe, new TEvSchemeShard::TEvNotifyTxCompletion(txId));
    }

    void Handle(TEvents::TEvPoisonPill::TPtr, const TActorContext &ctx) {
        if (SchemeShardPipe) {
            NTabletPipe::CloseClient(ctx, SchemeShardPipe);
        }
        Die(ctx);
    }

private:
    ui64 SchemeshardTabletId;
    TActorId SchemeShardPipe;
    THashMap<ui64, THashSet<TActorId>> SchemeTxWaiters;
};


// Automatically resend notification requests to Schemeshard if it gets restarted
class TFakeMetering : public TActor<TFakeMetering> {
    TVector<TString> Jsons;

public:
    explicit TFakeMetering()
        : TActor<TFakeMetering>(&TFakeMetering::StateWork)
    {}

private:
    STFUNC(StateWork) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvents::TEvPoisonPill, HandlePoisonPill);
            HFunc(NMetering::TEvMetering::TEvWriteMeteringJson, HandleWriteMeteringJson);
        default:
            HandleUnexpectedEvent(ev, ctx);
            break;
        }
    }

    void HandlePoisonPill(const TEvents::TEvPoisonPill::TPtr& ev, const TActorContext& ctx) {
        Y_UNUSED(ev);
        Die(ctx);
    }

    void HandleWriteMeteringJson(
        const NMetering::TEvMetering::TEvWriteMeteringJson::TPtr& ev,
        const TActorContext& ctx)
    {
        Y_UNUSED(ctx);

        LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                    "tests -- TFakeMetering got TEvMetering::TEvWriteMeteringJson");

        const auto* msg = ev->Get();

        Jsons.push_back(msg->MeteringJson);
    }

    void HandleUnexpectedEvent(STFUNC_SIG)
    {
        Y_UNUSED(ctx);

        LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                    "TFakeMetering:"
                        << " unhandled event type: " << ev->GetTypeRewrite()
                        << " event: " << (ev->HasEvent() ? ev->GetBase()->ToString().data() : "serialized?"));
    }

private:
};


// Automatically resend transaction requests to Schemeshard if it gets restarted
class TTxReliablePropose : public TActor<TTxReliablePropose> {
public:
    explicit TTxReliablePropose(ui64 schemeshardTabletId)
        : TActor<TTxReliablePropose>(&TTxReliablePropose::StateWork)
          , SchemeshardTabletId(schemeshardTabletId)
    {}

private:
    using TPreSerialisedMessage = std::pair<ui32, TIntrusivePtr<TEventSerializedData>>; // ui32 it's a type

private:
    void StateWork(TAutoPtr<NActors::IEventHandle> &ev, const NActors::TActorContext &ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTabletPipe::TEvClientConnected, Handle);
            HFunc(TEvTabletPipe::TEvClientDestroyed, Handle);

            HFunc(TEvSchemeShard::TEvModifySchemeTransaction, Handle);
            HFunc(TEvSchemeShard::TEvModifySchemeTransactionResult, Handle);

            HFunc(TEvSchemeShard::TEvCancelTx, Handle);
            HFunc(TEvSchemeShard::TEvCancelTxResult, Handle);

            HFunc(TEvExport::TEvCancelExportRequest, Handle);
            HFunc(TEvExport::TEvCancelExportResponse, Handle);

            HFunc(TEvExport::TEvForgetExportRequest, Handle);
            HFunc(TEvExport::TEvForgetExportResponse, Handle);

            HFunc(TEvImport::TEvCancelImportRequest, Handle);
            HFunc(TEvImport::TEvCancelImportResponse, Handle);
        };
    }

    void Handle(TEvTabletPipe::TEvClientConnected::TPtr &ev, const TActorContext &ctx) {
        TEvTabletPipe::TEvClientConnected *msg = ev->Get();
        if (msg->Status == NKikimrProto::OK)
            return;

        DoHandleDisconnect(ev->Get()->ClientId, ctx);
    }

    void Handle(TEvTabletPipe::TEvClientDestroyed::TPtr &ev, const TActorContext &ctx) {
        DoHandleDisconnect(ev->Get()->ClientId, ctx);
    }

    void DoHandleDisconnect(TActorId pipeClient, const TActorContext &ctx) {
        if (pipeClient == SchemeShardPipe) {
            SchemeShardPipe = TActorId();
            // Resend all
            for (const auto& w : SchemeTxWaiters) {
                SendToSchemeshard(w.first, ctx);
            }
        }
    }

    template<class TEventPtr>
    void HandleRequest(TEventPtr &ev, const TActorContext &ctx) {
        ui64 txId = ev->Get()->Record.GetTxId();
        if (SchemeTxWaiters.contains(txId))
            return;

        // Save TxId, forward to schemeshard
        SchemeTxWaiters[txId] = ev->Sender;
        OnlineRequests[txId] = GetSerialisedMessage(ev->ReleaseBase());
        SendToSchemeshard(txId, ctx);
    }

    void Handle(TEvSchemeShard::TEvModifySchemeTransaction::TPtr &ev, const TActorContext &ctx) {
        HandleRequest(ev, ctx);
    }

    void Handle(TEvSchemeShard::TEvCancelTx::TPtr &ev, const TActorContext &ctx) {
        HandleRequest(ev, ctx);
    }

    void Handle(TEvExport::TEvCancelExportRequest::TPtr &ev, const TActorContext &ctx) {
        HandleRequest(ev, ctx);
    }

    void Handle(TEvExport::TEvForgetExportRequest::TPtr &ev, const TActorContext &ctx) {
        HandleRequest(ev, ctx);
    }

    void Handle(TEvImport::TEvCancelImportRequest::TPtr &ev, const TActorContext &ctx) {
        HandleRequest(ev, ctx);
    }

    template<class TEventPtr>
    void HandleResponse(TEventPtr &ev, const TActorContext &ctx) {
        ui64 txId = ev->Get()->Record.GetTxId();
        if (!SchemeTxWaiters.contains(txId))
            return;

        ctx.Send(SchemeTxWaiters[txId], ev->ReleaseBase().Release());

        SchemeTxWaiters.erase(txId);
        OnlineRequests.erase(txId);
    }

    void Handle(TEvSchemeShard::TEvModifySchemeTransactionResult::TPtr &ev, const TActorContext &ctx) {
        HandleResponse(ev, ctx);
    }

    void Handle(TEvSchemeShard::TEvCancelTxResult::TPtr &ev, const TActorContext &ctx) {
        HandleResponse(ev, ctx);
    }

    void Handle(TEvExport::TEvCancelExportResponse::TPtr &ev, const TActorContext &ctx) {
        HandleResponse(ev, ctx);
    }

    void Handle(TEvExport::TEvForgetExportResponse::TPtr &ev, const TActorContext &ctx) {
        HandleResponse(ev, ctx);
    }

    void Handle(TEvImport::TEvCancelImportResponse::TPtr &ev, const TActorContext &ctx) {
        HandleResponse(ev, ctx);
    }

    void SendToSchemeshard(ui64 txId, const TActorContext &ctx) {
        if (!SchemeShardPipe) {
            SchemeShardPipe = ctx.Register(NTabletPipe::CreateClient(ctx.SelfID, SchemeshardTabletId, GetPipeConfigWithRetries()));
        }

        TPreSerialisedMessage& preSerialisedMessages = OnlineRequests[txId];
        NTabletPipe::SendData(ctx, SchemeShardPipe, preSerialisedMessages.first, preSerialisedMessages.second, 0);
    }

    void Handle(TEvents::TEvPoisonPill::TPtr, const TActorContext &ctx) {
        if (SchemeShardPipe) {
            NTabletPipe::CloseClient(ctx, SchemeShardPipe);
        }
        Die(ctx);
    }

    TPreSerialisedMessage GetSerialisedMessage(TAutoPtr<IEventBase> message) {
        TAllocChunkSerializer serializer;
        const bool success = message->SerializeToArcadiaStream(&serializer);
        Y_VERIFY(success);
        TIntrusivePtr<TEventSerializedData> data = serializer.Release(message->IsExtendedFormat());
        return TPreSerialisedMessage(message->Type(), data);
    }

private:
    ui64 SchemeshardTabletId;
    TActorId SchemeShardPipe;
    THashMap<ui64, TPreSerialisedMessage> OnlineRequests;
    THashMap<ui64, TActorId> SchemeTxWaiters;
};


// Globally enable/disable log batching at datashard creation time in test
struct TDatashardLogBatchingSwitch {
    explicit TDatashardLogBatchingSwitch(bool newVal) {
        PrevVal = NKikimr::NDataShard::gAllowLogBatchingDefaultValue;
        NKikimr::NDataShard::gAllowLogBatchingDefaultValue = newVal;
    }

    ~TDatashardLogBatchingSwitch() {
        NKikimr::NDataShard::gAllowLogBatchingDefaultValue = PrevVal;
    }
private:
    bool PrevVal;
};

NSchemeShardUT_Private::TTestEnv::TTestEnv(TTestActorRuntime& runtime, const TTestEnvOptions& opts, TSchemeShardFactory ssFactory, std::shared_ptr<NKikimr::NDataShard::IExportFactory> dsExportFactory)
    : SchemeShardFactory(ssFactory)
    , HiveState(new TFakeHiveState)
    , CoordinatorState(new TFakeCoordinator::TState)
    , ChannelsCount(opts.NChannels_)
{
    ui64 hive = TTestTxConfig::Hive;
    ui64 schemeRoot = TTestTxConfig::SchemeShard;
    ui64 coordinator = TTestTxConfig::Coordinator;
    ui64 txAllocator = TTestTxConfig::TxAllocator;

    TAppPrepare app(dsExportFactory ? dsExportFactory : static_cast<std::shared_ptr<NKikimr::NDataShard::IExportFactory>>(std::make_shared<TDataShardExportFactory>()));

    app.SetEnableDataColumnForIndexTable(true);
    app.SetEnableSystemViews(opts.EnableSystemViews_);
    app.SetEnablePersistentPartitionStats(opts.EnablePersistentPartitionStats_);
    app.SetAllowUpdateChannelsBindingOfSolomonPartitions(opts.AllowUpdateChannelsBindingOfSolomonPartitions_);
    app.SetEnableNotNullColumns(opts.EnableNotNullColumns_);
    app.SetEnableOlapSchemaOperations(opts.EnableOlapSchemaOperations_);
    app.SetEnableProtoSourceIdInfo(opts.EnableProtoSourceIdInfo_);
    app.SetEnablePqBilling(opts.EnablePqBilling_);
    app.SetEnableBackgroundCompaction(opts.EnableBackgroundCompaction_);
    app.FeatureFlags.SetEnablePublicApiExternalBlobs(true);
    app.SetEnableMoveIndex(opts.EnableMoveIndex_);

    if (opts.DisableStatsBatching_.value_or(false)) {
        app.SchemeShardConfig.SetStatsMaxBatchSize(0);
        app.SchemeShardConfig.SetStatsBatchTimeoutMs(0);
    }

    for (const auto& sid : opts.SystemBackupSIDs_) {
        app.AddSystemBackupSID(sid);
    }

    AddDomain(runtime, app, TTestTxConfig::DomainUid, 0, hive, schemeRoot);

    SetupLogging(runtime);
    SetupChannelProfiles(app, TTestTxConfig::DomainUid, ChannelsCount);

    for (ui32 node = 0; node < runtime.GetNodeCount(); ++node) {
        SetupSchemeCache(runtime, node, app.Domains->GetDomain(TTestTxConfig::DomainUid).Name);
    }

    SetupTabletServices(runtime, &app);
    if (opts.EnablePipeRetries_) {
        EnableSchemeshardPipeRetriesGuard = EnableSchemeshardPipeRetries(runtime);
    }

    TActorId sender = runtime.AllocateEdgeActor();
    //CreateTestBootstrapper(runtime, CreateTestTabletInfo(MakeBSControllerID(TTestTxConfig::DomainUid), TTabletTypes::BSController), &CreateFlatBsController);
    BootSchemeShard(runtime, schemeRoot);
    BootTxAllocator(runtime, txAllocator);
    BootFakeCoordinator(runtime, coordinator, CoordinatorState);
    BootFakeHive(runtime, hive, HiveState, &GetTabletCreationFunc);

    InitRootStoragePools(runtime, schemeRoot, sender, TTestTxConfig::DomainUid);

    for (ui32 node = 0; node < runtime.GetNodeCount(); ++node) {
        IActor* txProxy = CreateTxProxy(runtime.GetTxAllocatorTabletIds());
        TActorId txProxyId = runtime.Register(txProxy, node);
        runtime.RegisterService(MakeTxProxyID(), txProxyId, node);
    }

    //SetupBoxAndStoragePool(runtime, sender, TTestTxConfig::DomainUid);

    TxReliablePropose = runtime.Register(new TTxReliablePropose(schemeRoot));
    CreateFakeMetering(runtime);

    SetSplitMergePartCountLimit(&runtime, -1);
}

NSchemeShardUT_Private::TTestEnv::TTestEnv(TTestActorRuntime &runtime, ui32 nchannels, bool enablePipeRetries,
        NSchemeShardUT_Private::TTestEnv::TSchemeShardFactory ssFactory, bool enableSystemViews)
    : TTestEnv(runtime, TTestEnvOptions()
        .NChannels(nchannels)
        .EnablePipeRetries(enablePipeRetries)
        .EnableSystemViews(enableSystemViews), ssFactory)
{
}

void NSchemeShardUT_Private::TTestEnv::SetupLogging(TTestActorRuntime &runtime) {
    runtime.SetLogPriority(NKikimrServices::PERSQUEUE, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::BS_CONTROLLER, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::PIPE_CLIENT, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::PIPE_SERVER, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TABLET_MAIN, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TABLET_RESOLVER, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TX_PROXY, NActors::NLog::PRI_DEBUG);
    runtime.SetLogPriority(NKikimrServices::HIVE, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::SCHEME_BOARD_POPULATOR, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::SCHEME_BOARD_REPLICA, NActors::NLog::PRI_ERROR);


    runtime.SetLogPriority(NKikimrServices::FLAT_TX_SCHEMESHARD, NActors::NLog::PRI_NOTICE);
    runtime.SetLogPriority(NKikimrServices::SCHEMESHARD_DESCRIBE, NActors::NLog::PRI_NOTICE);
    runtime.SetLogPriority(NKikimrServices::BUILD_INDEX, NActors::NLog::PRI_NOTICE);
    if (ENABLE_SCHEMESHARD_LOG) {
        runtime.SetLogPriority(NKikimrServices::FLAT_TX_SCHEMESHARD, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::SCHEMESHARD_DESCRIBE, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::BUILD_INDEX, NActors::NLog::PRI_TRACE);
        runtime.SetLogPriority(NKikimrServices::HIVE, NActors::NLog::PRI_DEBUG);
    }

    runtime.SetLogPriority(NKikimrServices::EXPORT, NActors::NLog::PRI_NOTICE);
    runtime.SetLogPriority(NKikimrServices::DATASHARD_BACKUP, NActors::NLog::PRI_NOTICE);
    if (ENABLE_EXPORT_LOG) {
        runtime.SetLogPriority(NKikimrServices::EXPORT, NActors::NLog::PRI_TRACE);
        runtime.SetLogPriority(NKikimrServices::DATASHARD_BACKUP, NActors::NLog::PRI_TRACE);
    }

    if (ENABLE_SCHEMEBOARD_LOG) {
        runtime.SetLogPriority(NKikimrServices::SCHEME_BOARD_POPULATOR, NActors::NLog::PRI_TRACE);
        runtime.SetLogPriority(NKikimrServices::SCHEME_BOARD_REPLICA, NActors::NLog::PRI_TRACE);
    }

    runtime.SetLogPriority(NKikimrServices::CHANGE_EXCHANGE, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TX_DATASHARD, NActors::NLog::PRI_ERROR);
    if (ENABLE_DATASHARD_LOG) {
        runtime.SetLogPriority(NKikimrServices::CHANGE_EXCHANGE, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_DATASHARD, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::HIVE, NActors::NLog::PRI_DEBUG);
    }

    runtime.SetLogPriority(NKikimrServices::TX_COORDINATOR, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR_TIMECAST, NActors::NLog::PRI_ERROR);
    runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR_TABLETQUEUE, NActors::NLog::PRI_ERROR);
    if (ENABLE_COORDINATOR_MEDIATOR_LOG) {
        runtime.SetLogPriority(NKikimrServices::TX_COORDINATOR, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR_TIMECAST, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_MEDIATOR_TABLETQUEUE, NActors::NLog::PRI_DEBUG);
    }

    runtime.SetLogPriority(NKikimrServices::TX_OLAPSHARD, NActors::NLog::PRI_NOTICE);
    runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD, NActors::NLog::PRI_NOTICE);
    if (ENABLE_OLAP_LOG) {
        runtime.SetLogPriority(NKikimrServices::TX_OLAPSHARD, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD, NActors::NLog::PRI_DEBUG);
    }
}

void NSchemeShardUT_Private::TTestEnv::AddDomain(TTestActorRuntime &runtime, TAppPrepare &app, ui32 domainUid, ui32 ssId, ui64 hive, ui64 schemeRoot) {
    app.ClearDomainsAndHive();
    ui32 planResolution = 50;
    auto domain = TDomainsInfo::TDomain::ConstructDomainWithExplicitTabletIds(
                "MyRoot", domainUid, schemeRoot,
                ssId, ssId, TVector<ui32>{ssId},
                domainUid, TVector<ui32>{domainUid},
                planResolution,
                TVector<ui64>{TDomainsInfo::MakeTxCoordinatorIDFixed(domainUid, 1)},
                TVector<ui64>{},
                TVector<ui64>{TDomainsInfo::MakeTxAllocatorIDFixed(domainUid, 1)},
                DefaultPoolKinds(2));

    TVector<ui64> ids = runtime.GetTxAllocatorTabletIds();
    ids.insert(ids.end(), domain->TxAllocators.begin(), domain->TxAllocators.end());
    runtime.SetTxAllocatorTabletIds(ids);

    app.AddDomain(domain.Release());
    app.AddHive(domainUid, hive);
}

TFakeHiveState::TPtr NSchemeShardUT_Private::TTestEnv::GetHiveState() const {
    return HiveState;
}

TAutoPtr<ITabletScheduledEventsGuard> NSchemeShardUT_Private::TTestEnv::EnableSchemeshardPipeRetries(TTestActorRuntime &runtime) {
    TActorId sender = runtime.AllocateEdgeActor();
    TVector<ui64> tabletIds;
    // Add schemeshard tabletId to white list
    tabletIds.push_back((ui64)TTestTxConfig::SchemeShard);
    return CreateTabletScheduledEventsGuard(tabletIds, runtime, sender);
}

NActors::TActorId NSchemeShardUT_Private::CreateNotificationSubscriber(NActors::TTestActorRuntime &runtime, ui64 schemeshardId) {
    return runtime.Register(new TTxNotificationSubscriber(schemeshardId));
}

NActors::TActorId NSchemeShardUT_Private::CreateFakeMetering(NActors::TTestActorRuntime &runtime) {
    NActors::TActorId actorId = runtime.Register(new TFakeMetering());
    runtime.RegisterService(NMetering::MakeMeteringServiceID(), actorId);
    return NMetering::MakeMeteringServiceID();
}

void NSchemeShardUT_Private::TestWaitNotification(NActors::TTestActorRuntime &runtime, TSet<ui64> txIds, TActorId subscriberActorId) {

    TActorId sender = runtime.AllocateEdgeActor();

    for (ui64 txId : txIds) {
        Cerr << Endl << "TestWaitNotification wait txId: " << txId << Endl;
        auto ev = new TEvSchemeShard::TEvNotifyTxCompletion(txId);
        runtime.Send(new IEventHandle(subscriberActorId, sender, ev));
    }

    TAutoPtr<IEventHandle> handle;
    while (txIds.size()) {
        auto event = runtime.GrabEdgeEvent<TEvSchemeShard::TEvNotifyTxCompletionResult>(handle);
        UNIT_ASSERT(event);
        ui64 eventTxId = event->Record.GetTxId();
        Cerr << Endl << "TestWaitNotification: OK eventTxId " << eventTxId << Endl;
        UNIT_ASSERT(txIds.find(eventTxId) != txIds.end());
        txIds.erase(eventTxId);
    }
}

void NSchemeShardUT_Private::TTestEnv::TestWaitNotification(NActors::TTestActorRuntime &runtime, TSet<ui64> txIds, ui64 schemeshardId) {
    if (!TxNotificationSubcribers.contains(schemeshardId)) {
        TxNotificationSubcribers[schemeshardId] = CreateNotificationSubscriber(runtime, schemeshardId);
    }

    NSchemeShardUT_Private::TestWaitNotification(runtime, txIds, TxNotificationSubcribers.at(schemeshardId));
}

void NSchemeShardUT_Private::TTestEnv::TestWaitNotification(TTestActorRuntime &runtime, int txId, ui64 schemeshardId) {
    TestWaitNotification(runtime, (ui64)txId, schemeshardId);
}

void NSchemeShardUT_Private::TTestEnv::TestWaitNotification(NActors::TTestActorRuntime &runtime, ui64 txId, ui64 schemeshardId) {
    TSet<ui64> ids;
    ids.insert(txId);
    TestWaitNotification(runtime, ids, schemeshardId);
}

void NSchemeShardUT_Private::TTestEnv::TestWaitTabletDeletion(NActors::TTestActorRuntime &runtime, TSet<ui64> tabletIds) {
    TActorId sender = runtime.AllocateEdgeActor();

    for (ui64 tabletId : tabletIds) {
        Cerr << "wait until " << tabletId << " is deleted" << Endl;
        auto ev = new TEvFakeHive::TEvSubscribeToTabletDeletion(tabletId);
        ForwardToTablet(runtime, TTestTxConfig::Hive, sender, ev);
    }

    TAutoPtr<IEventHandle> handle;
    while (tabletIds.size()) {
        auto event = runtime.GrabEdgeEvent<TEvHive::TEvResponseHiveInfo>(handle);
        UNIT_ASSERT(event);
        UNIT_ASSERT_VALUES_EQUAL(event->Record.TabletsSize(), 1);

        ui64 tabletId = event->Record.GetTablets(0).GetTabletID();
        Cerr << Endl << "Deleted tabletId " << tabletId << Endl;
        UNIT_ASSERT(tabletIds.find(tabletId) != tabletIds.end());
        tabletIds.erase(tabletId);
    }
}

void NSchemeShardUT_Private::TTestEnv::TestWaitTabletDeletion(NActors::TTestActorRuntime &runtime, ui64 tabletId) {
    TestWaitTabletDeletion(runtime, TSet<ui64>{tabletId});
}

void NSchemeShardUT_Private::TTestEnv::TestWaitShardDeletion(NActors::TTestActorRuntime &runtime, ui64 schemeShard, TSet<TShardIdx> shardIds) {
    TActorId sender = runtime.AllocateEdgeActor();

    for (auto shardIdx : shardIds) {
        Cerr << "Waiting until shard idx " << shardIdx << " is deleted" << Endl;
        auto ev = new TEvPrivate::TEvSubscribeToShardDeletion(shardIdx);
        ForwardToTablet(runtime, schemeShard, sender, ev);
    }

    while (!shardIds.empty()) {
        auto ev = runtime.GrabEdgeEvent<TEvPrivate::TEvNotifyShardDeleted>(sender);
        auto shardIdx = ev->Get()->ShardIdx;
        Cerr << "Deleted shard idx " << shardIdx << Endl;
        shardIds.erase(shardIdx);
    }
}

void NSchemeShardUT_Private::TTestEnv::TestWaitShardDeletion(NActors::TTestActorRuntime &runtime, ui64 schemeShard, TSet<ui64> localIds) {
    TSet<TShardIdx> shardIds;
    for (ui64 localId : localIds) {
        shardIds.emplace(schemeShard, localId);
    }
    TestWaitShardDeletion(runtime, schemeShard, std::move(shardIds));
}

void NSchemeShardUT_Private::TTestEnv::TestWaitShardDeletion(NActors::TTestActorRuntime &runtime, TSet<ui64> localIds) {
    TestWaitShardDeletion(runtime, TTestTxConfig::SchemeShard, std::move(localIds));
}

void NSchemeShardUT_Private::TTestEnv::SimulateSleep(NActors::TTestActorRuntime &runtime, TDuration duration) {
    auto sender = runtime.AllocateEdgeActor();
    runtime.Schedule(new IEventHandle(sender, sender, new TEvents::TEvWakeup()), duration);
    runtime.GrabEdgeEventRethrow<TEvents::TEvWakeup>(sender);
}

std::function<NActors::IActor *(const NActors::TActorId &, NKikimr::TTabletStorageInfo *)> NSchemeShardUT_Private::TTestEnv::GetTabletCreationFunc(ui32 type) {
    switch (type) {
    case TTabletTypes::BlockStoreVolume:
        return [](const TActorId& tablet, TTabletStorageInfo* info) {
            return new TFakeBlockStoreVolume(tablet, info);
        };
    case TTabletTypes::FileStore:
        return [](const TActorId& tablet, TTabletStorageInfo* info) {
            return new TFakeFileStore(tablet, info);
        };
    default:
        return nullptr;
    }
}

TEvSchemeShard::TEvInitRootShardResult::EStatus NSchemeShardUT_Private::TTestEnv::InitRoot(NActors::TTestActorRuntime &runtime, ui64 schemeRoot, const NActors::TActorId &sender, const TString& domainName, const TDomainsInfo::TDomain::TStoragePoolKinds& StoragePoolTypes, const TString& owner) {
    auto ev = new TEvSchemeShard::TEvInitRootShard(sender, 32, domainName);
    for (const auto& [kind, pool] : StoragePoolTypes) {
        auto* p = ev->Record.AddStoragePools();
        p->SetKind(kind);
        p->SetName(pool.GetName());
    }
    if (owner) {
        ev->Record.SetOwner(owner);
    }

    runtime.SendToPipe(schemeRoot, sender, ev, 0, GetPipeConfigWithRetries());

    TAutoPtr<IEventHandle> handle;
    auto event = runtime.GrabEdgeEvent<TEvSchemeShard::TEvInitRootShardResult>(handle);
    UNIT_ASSERT_VALUES_EQUAL(event->Record.GetOrigin(), schemeRoot);
    UNIT_ASSERT_VALUES_EQUAL(event->Record.GetOrigin(), schemeRoot);

    return (TEvSchemeShard::TEvInitRootShardResult::EStatus)event->Record.GetStatus();
}

void NSchemeShardUT_Private::TTestEnv::InitRootStoragePools(NActors::TTestActorRuntime &runtime, ui64 schemeRoot, const NActors::TActorId &sender, ui64 domainUid) {
    const TDomainsInfo::TDomain& domain = runtime.GetAppData().DomainsInfo->GetDomain(domainUid);

    auto evTx = new TEvSchemeShard::TEvModifySchemeTransaction(1, TTestTxConfig::SchemeShard);
    auto transaction = evTx->Record.AddTransaction();
    transaction->SetOperationType(NKikimrSchemeOp::EOperationType::ESchemeOpAlterSubDomain);
    transaction->SetWorkingDir("/");
    auto op = transaction->MutableSubDomain();
    op->SetName(domain.Name);

    for (const auto& [kind, pool] : domain.StoragePoolTypes) {
        auto* p = op->AddStoragePools();
        p->SetKind(kind);
        p->SetName(pool.GetName());
    }

    runtime.SendToPipe(schemeRoot, sender, evTx, 0, GetPipeConfigWithRetries());

    {
        TAutoPtr<IEventHandle> handle;
        auto event = runtime.GrabEdgeEvent<TEvSchemeShard::TEvModifySchemeTransactionResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(event->Record.GetSchemeshardId(), schemeRoot);
        UNIT_ASSERT_VALUES_EQUAL(event->Record.GetStatus(), NKikimrScheme::EStatus::StatusAccepted);
    }

    auto evSubscribe = new TEvSchemeShard::TEvNotifyTxCompletion(1);
    runtime.SendToPipe(schemeRoot, sender, evSubscribe, 0, GetPipeConfigWithRetries());

    {
        TAutoPtr<IEventHandle> handle;
        auto event = runtime.GrabEdgeEvent<TEvSchemeShard::TEvNotifyTxCompletionResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(event->Record.GetTxId(), 1);
    }
}


void NSchemeShardUT_Private::TTestEnv::BootSchemeShard(NActors::TTestActorRuntime &runtime, ui64 schemeRoot) {
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(schemeRoot, TTabletTypes::SchemeShard), SchemeShardFactory);
}

void NSchemeShardUT_Private::TTestEnv::BootTxAllocator(NActors::TTestActorRuntime &runtime, ui64 tabletId) {
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(tabletId, TTabletTypes::TxAllocator), &CreateTxAllocator);
}

NSchemeShardUT_Private::TTestWithReboots::TTestWithReboots(bool killOnCommit, NSchemeShardUT_Private::TTestEnv::TSchemeShardFactory ssFactory)
    : EnvOpts(GetDefaultTestEnvOptions())
    , SchemeShardFactory(ssFactory)
    , HiveTabletId(TTestTxConfig::Hive)
    , SchemeShardTabletId(TTestTxConfig::SchemeShard)
    , CoordinatorTabletId(TTestTxConfig::Coordinator)
    , TxAllocatorId(TTestTxConfig::TxAllocator)
    , KillOnCommit(killOnCommit)
{
    TabletIds.push_back(HiveTabletId);
    TabletIds.push_back(CoordinatorTabletId);
    TabletIds.push_back(SchemeShardTabletId);
    TabletIds.push_back(TxAllocatorId);

    ui64 datashard = TTestTxConfig::FakeHiveTablets;
    TabletIds.push_back(datashard+0);
    TabletIds.push_back(datashard+1);
    TabletIds.push_back(datashard+2);
    TabletIds.push_back(datashard+3);
    TabletIds.push_back(datashard+4);
    TabletIds.push_back(datashard+5);
    TabletIds.push_back(datashard+6);
    TabletIds.push_back(datashard+7);
    TabletIds.push_back(datashard+8);
}

void NSchemeShardUT_Private::TTestWithReboots::Run(std::function<void (TTestActorRuntime &, bool &)> testScenario) {
    Run(testScenario, true);
    Run(testScenario, false);
}

void NSchemeShardUT_Private::TTestWithReboots::Run(std::function<void (TTestActorRuntime &, bool &)> testScenario, bool allowLogBatching) {
    TDatashardLogBatchingSwitch logBatchingSwitch(allowLogBatching);

    RunWithTabletReboots(testScenario);
    RunWithPipeResets(testScenario);
    //RunWithDelays(testScenario);
}

struct NSchemeShardUT_Private::TTestWithReboots::TFinalizer {
    NSchemeShardUT_Private::TTestWithReboots& TestWithReboots;

    explicit TFinalizer(NSchemeShardUT_Private::TTestWithReboots& testContext)
        : TestWithReboots(testContext)
    {}

    ~TFinalizer() {
        TestWithReboots.Finalize();
    }
};

void NSchemeShardUT_Private::TTestWithReboots::RunWithTabletReboots(std::function<void (TTestActorRuntime &, bool &)> testScenario) {
    RunTestWithReboots(TabletIds,
                       [&]() {
        return PassUserRequests;
    },
    [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(*this);
        Prepare(dispatchName, setup, activeZone);

        activeZone = true;
        testScenario(*Runtime, activeZone);
    }, Max<ui32>(), Max<ui64>(), 0, 0, KillOnCommit);
}

void NSchemeShardUT_Private::TTestWithReboots::RunWithPipeResets(std::function<void (TTestActorRuntime &, bool &)> testScenario) {
    RunTestWithPipeResets(TabletIds,
                          [&]() {
        return PassUserRequests;
    },
    [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(*this);
        Prepare(dispatchName, setup, activeZone);

        activeZone = true;
        testScenario(*Runtime, activeZone);
    });
}

void NSchemeShardUT_Private::TTestWithReboots::RunWithDelays(std::function<void (TTestActorRuntime &, bool &)> testScenario) {
    RunTestWithDelays(TRunWithDelaysConfig(), TabletIds,
                      [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(*this);
        Prepare(dispatchName, setup, activeZone);

        activeZone = true;
        testScenario(*Runtime, activeZone);
    });
}

void NSchemeShardUT_Private::TTestWithReboots::RestoreLogging() {
    TestEnv->SetupLogging(*Runtime);
}

NSchemeShardUT_Private::TTestEnv* NSchemeShardUT_Private::TTestWithReboots::CreateTestEnv() {
    return new TTestEnv(*Runtime, GetTestEnvOptions());
}



void NSchemeShardUT_Private::TTestWithReboots::Prepare(const TString &dispatchName, std::function<void (TTestActorRuntime &)> setup, bool &outActiveZone) {
    Cdbg << Endl << "=========== RUN: "<< dispatchName << " ===========" << Endl;

    outActiveZone = false;

    Runtime.Reset(new TTestBasicRuntime);
    setup(*Runtime);

    //TestEnv.Reset(new TTestEnv(*Runtime, 4, false, SchemeShardFactory));

    TestEnv.Reset(CreateTestEnv());

    RestoreLogging();

    TxId = 1000;

    TestMkDir(*Runtime, TxId++, "/MyRoot", "DirA");

    // This allows tablet resolver to detect deleted tablets
    EnableTabletResolverScheduling();

    outActiveZone = true;
}

void NSchemeShardUT_Private::TTestWithReboots::EnableTabletResolverScheduling(ui32 nodeIdx) {
    auto actorId = Runtime->GetLocalServiceId(MakeTabletResolverID(), nodeIdx);
    Y_VERIFY(actorId);
    Runtime->EnableScheduleForActor(actorId);
}

void NSchemeShardUT_Private::TTestWithReboots::Finalize() {
    TestEnv.Reset();
    Runtime.Reset();
}

bool NSchemeShardUT_Private::TTestWithReboots::PassUserRequests(TTestActorRuntimeBase &runtime, TAutoPtr<IEventHandle> &event) {
    Y_UNUSED(runtime);
    return event->Type == TEvSchemeShard::EvModifySchemeTransaction ||
           event->Type == TEvSchemeShard::EvDescribeScheme ||
           event->Type == TEvSchemeShard::EvNotifyTxCompletion ||
           event->Type == TEvSchemeShard::EvMeasureSelfResponseTime ||
           event->Type == TEvSchemeShard::EvWakeupToMeasureSelfResponseTime ||
           event->Type == TEvTablet::EvLocalMKQL ||
           event->Type == TEvFakeHive::EvSubscribeToTabletDeletion ||
           event->Type == TEvSchemeShard::EvCancelTx ||
           event->Type == TEvExport::EvCreateExportRequest ||
           event->Type == TEvIndexBuilder::EvCreateRequest ||
           event->Type == TEvIndexBuilder::EvGetRequest ||
           event->Type == TEvIndexBuilder::EvCancelRequest ||
           event->Type == TEvIndexBuilder::EvForgetRequest
        ;
}

NSchemeShardUT_Private::TTestEnvOptions& NSchemeShardUT_Private::TTestWithReboots::GetTestEnvOptions() {
    return EnvOpts;
}

const NSchemeShardUT_Private::TTestEnvOptions& NSchemeShardUT_Private::TTestWithReboots::GetTestEnvOptions() const {
    return EnvOpts;
}

NSchemeShardUT_Private::TTestEnvOptions NSchemeShardUT_Private::TTestWithReboots::GetDefaultTestEnvOptions() {
    return TTestEnvOptions()
            .EnablePipeRetries(false)
            .EnableNotNullColumns(true)
            .EnableProtoSourceIdInfo(true)
            .DisableStatsBatching(true)
            .EnableMoveIndex(true);
}
