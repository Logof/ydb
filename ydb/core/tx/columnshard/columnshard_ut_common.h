#pragma once

#include "columnshard.h"
#include "columnshard_impl.h"

#include <ydb/core/formats/arrow_batch_builder.h>
#include <ydb/core/scheme/scheme_tabledefs.h>
#include <ydb/core/scheme/scheme_types_proto.h>
#include <ydb/core/testlib/tablet_helpers.h>
#include <ydb/core/testlib/test_client.h>
#include <library/cpp/testing/unittest/registar.h>


namespace NKikimr::NTxUT {

class TTester : public TNonCopyable {
public:
    static constexpr const ui64 FAKE_SCHEMESHARD_TABLET_ID = 4200;

    static void Setup(TTestActorRuntime& runtime);
};

namespace NTypeIds = NScheme::NTypeIds;
using TTypeId = NScheme::TTypeId;
using TTypeInfo = NScheme::TTypeInfo;

struct TTestSchema {
    static const constexpr char * DefaultTtlColumn = "saved_at";

    struct TStorageTier {
        TString Name;
        TString Codec;
        std::optional<int> CompressionLevel;
        std::optional<ui32> EvictAfterSeconds;
        TString TtlColumn;
        std::optional<NKikimrSchemeOp::TS3Settings> S3;

        TStorageTier(const TString& name = {})
            : Name(name)
            , TtlColumn(DefaultTtlColumn)
        {}

        NKikimrSchemeOp::EColumnCodec GetCodecId() const {
            if (Codec == "none") {
                return NKikimrSchemeOp::EColumnCodec::ColumnCodecPlain;
            } else if (Codec == "lz4") {
                return NKikimrSchemeOp::EColumnCodec::ColumnCodecLZ4;
            } else if (Codec == "zstd") {
                return NKikimrSchemeOp::EColumnCodec::ColumnCodecZSTD;
            }
            Y_VERIFY(false);
        }

        bool HasCodec() const {
            return !Codec.empty();
        }

        TStorageTier& SetCodec(const TString& codec, std::optional<int> level = {}) {
            Codec = codec;
            if (level) {
                CompressionLevel = *level;
            }
            return *this;
        }

        TStorageTier& SetTtl(ui32 seconds, const TString& column = DefaultTtlColumn) {
            EvictAfterSeconds = seconds;
            TtlColumn = column;
            return *this;
        }
    };

    struct TTableSpecials : public TStorageTier {
        std::vector<TStorageTier> Tiers;

        bool HasTiers() const {
            return !Tiers.empty();
        }

        bool HasTtl() const {
            return !HasTiers() && EvictAfterSeconds;
        }

        TTableSpecials WithCodec(const TString& codec) {
            TTableSpecials out = *this;
            out.SetCodec(codec);
            return out;
        }
    };

    static auto YdbSchema(const std::pair<TString, TTypeInfo>& firstKeyItem = {"timestamp", TTypeInfo(NTypeIds::Timestamp) }) {
        TVector<std::pair<TString, TTypeInfo>> schema = {
            // PK
            firstKeyItem,
            {"resource_type", TTypeInfo(NTypeIds::Utf8) },
            {"resource_id", TTypeInfo(NTypeIds::Utf8) },
            {"uid", TTypeInfo(NTypeIds::Utf8) },
            {"level", TTypeInfo(NTypeIds::Int32) },
            {"message", TTypeInfo(NTypeIds::Utf8) },
            {"json_payload", TTypeInfo(NTypeIds::Json) },
            {"ingested_at", TTypeInfo(NTypeIds::Timestamp) },
            {"saved_at", TTypeInfo(NTypeIds::Timestamp) },
            {"request_id", TTypeInfo(NTypeIds::Utf8) }
        };
        return schema;
    };

    static auto YdbExoticSchema() {
        TVector<std::pair<TString, TTypeInfo>> schema = {
            // PK
            {"timestamp", TTypeInfo(NTypeIds::Timestamp) },
            {"resource_type", TTypeInfo(NTypeIds::Utf8) },
            {"resource_id", TTypeInfo(NTypeIds::Utf8) },
            {"uid", TTypeInfo(NTypeIds::Utf8) },
            //
            {"level", TTypeInfo(NTypeIds::Int32) },
            {"message", TTypeInfo(NTypeIds::String4k) },
            {"json_payload", TTypeInfo(NTypeIds::JsonDocument) },
            {"ingested_at", TTypeInfo(NTypeIds::Timestamp) },
            {"saved_at", TTypeInfo(NTypeIds::Timestamp) },
            {"request_id", TTypeInfo(NTypeIds::Yson) }
        };
        return schema;
    };

    static auto YdbPkSchema() {
        TVector<std::pair<TString, TTypeInfo>> schema = {
            {"timestamp", TTypeInfo(NTypeIds::Timestamp) },
            {"resource_type", TTypeInfo(NTypeIds::Utf8) },
            {"resource_id", TTypeInfo(NTypeIds::Utf8) },
            {"uid", TTypeInfo(NTypeIds::Utf8) }
        };
        return schema;
    }

    static auto YdbAllTypesSchema() {
        TVector<std::pair<TString, TTypeInfo>> schema = {
            { "ts", TTypeInfo(NTypeIds::Timestamp) },

            { "i8", TTypeInfo(NTypeIds::Int8) },
            { "i16", TTypeInfo(NTypeIds::Int16) },
            { "i32", TTypeInfo(NTypeIds::Int32) },
            { "i64", TTypeInfo(NTypeIds::Int64) },
            { "u8", TTypeInfo(NTypeIds::Uint8) },
            { "u16", TTypeInfo(NTypeIds::Uint16) },
            { "u32", TTypeInfo(NTypeIds::Uint32) },
            { "u64", TTypeInfo(NTypeIds::Uint64) },
            { "float", TTypeInfo(NTypeIds::Float) },
            { "double", TTypeInfo(NTypeIds::Double) },

            { "byte", TTypeInfo(NTypeIds::Byte) },
            //{ "bool", TTypeInfo(NTypeIds::Bool) },
            //{ "decimal", TTypeInfo(NTypeIds::Decimal) },
            //{ "dynum", TTypeInfo(NTypeIds::DyNumber) },

            { "date", TTypeInfo(NTypeIds::Date) },
            { "datetime", TTypeInfo(NTypeIds::Datetime) },
            //{ "interval", TTypeInfo(NTypeIds::Interval) },

            {"text", TTypeInfo(NTypeIds::Text) },
            {"bytes", TTypeInfo(NTypeIds::Bytes) },
            {"yson", TTypeInfo(NTypeIds::Yson) },
            {"json", TTypeInfo(NTypeIds::Json) },
            {"jsondoc", TTypeInfo(NTypeIds::JsonDocument) }
        };
        return schema;
    };

    static NKikimrSchemeOp::TOlapColumnDescription CreateColumn(ui32 id, const TString& name, TTypeInfo type) {
        NKikimrSchemeOp::TOlapColumnDescription col;
        col.SetId(id);
        col.SetName(name);
        auto columnType = NScheme::ProtoColumnTypeFromTypeInfo(type);
        col.SetTypeId(columnType.TypeId);
        if (columnType.TypeInfo) {
            *col.MutableTypeInfo() = *columnType.TypeInfo;
        }
        return col;
    }

    static TString CreateTableTxBody(ui64 pathId, const TVector<std::pair<TString, TTypeInfo>>& columns,
                                     const TVector<std::pair<TString, TTypeInfo>>& pk,
                                     const TTableSpecials& specials = {}) {
        NKikimrTxColumnShard::TSchemaTxBody tx;
        auto* table = tx.MutableEnsureTables()->AddTables();
        table->SetPathId(pathId);

        { // preset
            auto* preset = table->MutableSchemaPreset();
            preset->SetId(1);
            preset->SetName("default");

            // schema

            auto* schema = preset->MutableSchema();
            schema->SetEngine(NKikimrSchemeOp::COLUMN_ENGINE_REPLACING_TIMESERIES);

            for (ui32 i = 0; i < columns.size(); ++i) {
                *schema->MutableColumns()->Add() = CreateColumn(i + 1, columns[i].first, columns[i].second);
            }

            Y_VERIFY(pk.size() == 4);
            for (auto& column : ExtractNames(pk)) {
                schema->AddKeyColumnNames(column);
            }

            if (specials.HasCodec()) {
                schema->MutableDefaultCompression()->SetCompressionCodec(specials.GetCodecId());
            }
            if (specials.CompressionLevel) {
                schema->MutableDefaultCompression()->SetCompressionLevel(*specials.CompressionLevel);
            }

            for (auto& tier : specials.Tiers) {
                auto* t = schema->AddStorageTiers();
                t->SetName(tier.Name);
                if (tier.HasCodec()) {
                    t->MutableCompression()->SetCompressionCodec(tier.GetCodecId());
                }
                if (tier.CompressionLevel) {
                    t->MutableCompression()->SetCompressionLevel(*tier.CompressionLevel);
                }
                if (tier.S3) {
                    t->MutableObjectStorage()->CopyFrom(*tier.S3);
                }
            }
        }

        if (specials.HasTiers()) {
            auto* ttlSettings = table->MutableTtlSettings();
            ttlSettings->SetVersion(1);
            auto* tiering = ttlSettings->MutableTiering();
            for (auto& tier : specials.Tiers) {
                auto* t = tiering->AddTiers();
                t->SetName(tier.Name);
                t->MutableEviction()->SetColumnName(tier.TtlColumn);
                t->MutableEviction()->SetExpireAfterSeconds(*tier.EvictAfterSeconds);
            }
        } else  if (specials.HasTtl()) {
            auto* ttlSettings = table->MutableTtlSettings();
            ttlSettings->SetVersion(1);
            auto* enable = ttlSettings->MutableEnabled();
            enable->SetColumnName(specials.TtlColumn);
            enable->SetExpireAfterSeconds(*specials.EvictAfterSeconds);
        }

        TString out;
        Y_PROTOBUF_SUPPRESS_NODISCARD tx.SerializeToString(&out);
        return out;
    }

    static TString AlterTableTxBody(ui64 pathId, ui32 version, const TTableSpecials& specials) {
        NKikimrTxColumnShard::TSchemaTxBody tx;
        auto* table = tx.MutableAlterTable();
        table->SetPathId(pathId);
        tx.MutableSeqNo()->SetRound(version);

        auto* ttlSettings = table->MutableTtlSettings();
        ttlSettings->SetVersion(version);

        if (specials.HasTiers()) {
            auto* tiering = ttlSettings->MutableTiering();
            for (auto& tier : specials.Tiers) {
                auto* t = tiering->AddTiers();
                t->SetName(tier.Name);
                t->MutableEviction()->SetColumnName(tier.TtlColumn);
                t->MutableEviction()->SetExpireAfterSeconds(*tier.EvictAfterSeconds);
            }
        } else if (specials.HasTtl()) {
            auto* enable = ttlSettings->MutableEnabled();
            enable->SetColumnName(specials.TtlColumn);
            enable->SetExpireAfterSeconds(*specials.EvictAfterSeconds);
        } else {
            ttlSettings->MutableDisabled();
        }

        TString out;
        Y_PROTOBUF_SUPPRESS_NODISCARD tx.SerializeToString(&out);
        return out;
    }

    static TString DropTableTxBody(ui64 pathId, ui32 version) {
        NKikimrTxColumnShard::TSchemaTxBody tx;
        tx.MutableDropTable()->SetPathId(pathId);
        tx.MutableSeqNo()->SetRound(version);

        TString out;
        Y_PROTOBUF_SUPPRESS_NODISCARD tx.SerializeToString(&out);
        return out;
    }

    static TString CommitTxBody(ui64 metaShard, const TVector<ui64>& writeIds) {
        NKikimrTxColumnShard::TCommitTxBody proto;
        proto.SetTxInitiator(metaShard);
        for (ui64 id : writeIds) {
            proto.AddWriteIds(id);
        }

        TString txBody;
        Y_PROTOBUF_SUPPRESS_NODISCARD proto.SerializeToString(&txBody);
        return txBody;
    }

    static TString TtlTxBody(const TVector<ui64>& pathIds, TString ttlColumnName, ui64 tsSeconds) {
        NKikimrTxColumnShard::TTtlTxBody proto;
        proto.SetTtlColumnName(ttlColumnName);
        proto.SetUnixTimeSeconds(tsSeconds);
        for (auto& pathId : pathIds) {
            proto.AddPathIds(pathId);
        }

        TString txBody;
        Y_PROTOBUF_SUPPRESS_NODISCARD proto.SerializeToString(&txBody);
        return txBody;
    }

    static TVector<TString> ExtractNames(const TVector<std::pair<TString, NScheme::TTypeInfo>>& columns) {
        TVector<TString> out;
        out.reserve(columns.size());
        for (auto& col : columns) {
            out.push_back(col.first);
        }
        return out;
    }

    static TVector<NScheme::TTypeInfo> ExtractTypes(const TVector<std::pair<TString, NScheme::TTypeInfo>>& columns) {
        TVector<NScheme::TTypeInfo> types;
        types.reserve(columns.size());
        for (auto& [name, type] : columns) {
            types.push_back(type);
        }
        return types;
    }
};

bool ProposeSchemaTx(TTestBasicRuntime& runtime, TActorId& sender, const TString& txBody, NOlap::TSnapshot snap);
void PlanSchemaTx(TTestBasicRuntime& runtime, TActorId& sender, NOlap::TSnapshot snap);
bool WriteData(TTestBasicRuntime& runtime, TActorId& sender, ui64 metaShard, ui64 writeId, ui64 tableId,
               const TString& data, std::shared_ptr<arrow::Schema> schema = {});
void ScanIndexStats(TTestBasicRuntime& runtime, TActorId& sender, const TVector<ui64>& pathIds,
                    NOlap::TSnapshot snap, ui64 scanId = 0);
void ProposeCommit(TTestBasicRuntime& runtime, TActorId& sender, ui64 metaShard, ui64 txId, const TVector<ui64>& writeIds);
void PlanCommit(TTestBasicRuntime& runtime, TActorId& sender, ui64 planStep, const TSet<ui64>& txIds);

inline void PlanCommit(TTestBasicRuntime& runtime, TActorId& sender, ui64 planStep, ui64 txId) {
    TSet<ui64> ids;
    ids.insert(txId);
    PlanCommit(runtime, sender, planStep, ids);
}

TString MakeTestBlob(std::pair<ui64, ui64> range, const TVector<std::pair<TString, NScheme::TTypeInfo>>& columns,
                     const THashSet<TString>& nullColumns = {});
TSerializedTableRange MakeTestRange(std::pair<ui64, ui64> range, bool inclusiveFrom, bool inclusiveTo,
                                    const TVector<std::pair<TString, NScheme::TTypeInfo>>& columns);

}
