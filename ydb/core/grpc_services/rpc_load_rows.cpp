#include "service_table.h"
#include <ydb/core/grpc_services/base/base.h>

#include "rpc_common.h"
#include "service_table.h"

#include <ydb/core/tx/tx_proxy/upload_rows_common_impl.h>
#include <ydb/library/yql/public/udf/udf_types.h>
#include <ydb/library/yql/minikql/dom/yson.h>
#include <ydb/library/yql/minikql/dom/json.h>
#include <ydb/library/yql/utils/utf8.h>
#include <ydb/library/yql/public/decimal/yql_decimal.h>

#include <ydb/library/binary_json/write.h>
#include <ydb/library/dynumber/dynumber.h>

#include <util/string/vector.h>
#include <util/generic/size_literals.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NActors;
using namespace Ydb;

namespace {

bool CheckValueData(NScheme::TTypeInfo type, const TCell& cell, TString& err) {
    bool ok = true;
    switch (type.GetTypeId()) {
    case NScheme::NTypeIds::Bool:
    case NScheme::NTypeIds::Int8:
    case NScheme::NTypeIds::Uint8:
    case NScheme::NTypeIds::Int16:
    case NScheme::NTypeIds::Uint16:
    case NScheme::NTypeIds::Int32:
    case NScheme::NTypeIds::Uint32:
    case NScheme::NTypeIds::Int64:
    case NScheme::NTypeIds::Uint64:
    case NScheme::NTypeIds::Float:
    case NScheme::NTypeIds::Double:
    case NScheme::NTypeIds::String:
        break;

    case NScheme::NTypeIds::Decimal:
        ok = !NYql::NDecimal::IsError(cell.AsValue<NYql::NDecimal::TInt128>());
        break;

    case NScheme::NTypeIds::Date:
        ok = cell.AsValue<ui16>() < NUdf::MAX_DATE;
        break;

    case NScheme::NTypeIds::Datetime:
        ok = cell.AsValue<ui32>() < NUdf::MAX_DATETIME;
        break;

    case NScheme::NTypeIds::Timestamp:
        ok = cell.AsValue<ui64>() < NUdf::MAX_TIMESTAMP;
        break;

    case NScheme::NTypeIds::Interval:
        ok = (ui64)std::abs(cell.AsValue<i64>()) < NUdf::MAX_TIMESTAMP;
        break;

    case NScheme::NTypeIds::Utf8:
        ok = NYql::IsUtf8(cell.AsBuf());
        break;

    case NScheme::NTypeIds::Yson:
        ok = NYql::NDom::IsValidYson(cell.AsBuf());
        break;

    case NScheme::NTypeIds::Json:
        ok = NYql::NDom::IsValidJson(cell.AsBuf());
        break;

    case NScheme::NTypeIds::JsonDocument:
        // JsonDocument value was verified at parsing time
        break;

    case NScheme::NTypeIds::DyNumber:
        // DyNumber value was verified at parsing time
        break;

    case NScheme::NTypeIds::Pg:
        // no pg validation here
        break;

    default:
        err = Sprintf("Unexpected type %d", type.GetTypeId());
        return false;
    }

    if (!ok) {
        err = Sprintf("Invalid %s value", NScheme::TypeName(type));
    }

    return ok;
}


// TODO: no mapping for DATE, DATETIME, TZ_*, YSON, JSON, UUID, JSON_DOCUMENT, DYNUMBER
bool ConvertArrowToYdbPrimitive(const arrow::DataType& type, Ydb::Type& toType) {
    switch (type.id()) {
        case arrow::Type::BOOL:
            toType.set_type_id(Ydb::Type::BOOL);
            return true;
        case arrow::Type::UINT8:
            toType.set_type_id(Ydb::Type::UINT8);
            return true;
        case arrow::Type::INT8:
            toType.set_type_id(Ydb::Type::INT8);
            return true;
        case arrow::Type::UINT16:
            toType.set_type_id(Ydb::Type::UINT16);
            return true;
        case arrow::Type::INT16:
            toType.set_type_id(Ydb::Type::INT16);
            return true;
        case arrow::Type::UINT32:
            toType.set_type_id(Ydb::Type::UINT32);
            return true;
        case arrow::Type::INT32:
            toType.set_type_id(Ydb::Type::INT32);
            return true;
        case arrow::Type::UINT64:
            toType.set_type_id(Ydb::Type::UINT64);
            return true;
        case arrow::Type::INT64:
            toType.set_type_id(Ydb::Type::INT64);
            return true;
        case arrow::Type::FLOAT:
            toType.set_type_id(Ydb::Type::FLOAT);
            return true;
        case arrow::Type::DOUBLE:
            toType.set_type_id(Ydb::Type::DOUBLE);
            return true;
        case arrow::Type::STRING:
            toType.set_type_id(Ydb::Type::UTF8);
            return true;
        case arrow::Type::BINARY:
            toType.set_type_id(Ydb::Type::STRING);
            return true;
        case arrow::Type::TIMESTAMP:
            toType.set_type_id(Ydb::Type::TIMESTAMP);
            return true;
        case arrow::Type::DURATION:
            toType.set_type_id(Ydb::Type::INTERVAL);
            return true;
        case arrow::Type::DECIMAL:
            // TODO
            return false;
        case arrow::Type::NA:
        case arrow::Type::HALF_FLOAT:
        case arrow::Type::FIXED_SIZE_BINARY:
        case arrow::Type::DATE32:
        case arrow::Type::DATE64:
        case arrow::Type::TIME32:
        case arrow::Type::TIME64:
        case arrow::Type::INTERVAL_MONTHS:
        case arrow::Type::LARGE_STRING:
        case arrow::Type::LARGE_BINARY:
        case arrow::Type::DECIMAL256:
        case arrow::Type::DENSE_UNION:
        case arrow::Type::DICTIONARY:
        case arrow::Type::EXTENSION:
        case arrow::Type::FIXED_SIZE_LIST:
        case arrow::Type::INTERVAL_DAY_TIME:
        case arrow::Type::LARGE_LIST:
        case arrow::Type::LIST:
        case arrow::Type::MAP:
        case arrow::Type::MAX_ID:
        case arrow::Type::SPARSE_UNION:
        case arrow::Type::STRUCT:
            break;
    }
    return false;
}

}

using TEvBulkUpsertRequest = TGrpcRequestOperationCall<Ydb::Table::BulkUpsertRequest,
    Ydb::Table::BulkUpsertResponse>;

const Ydb::Table::BulkUpsertRequest* GetProtoRequest(IRequestOpCtx* req) {
    return TEvBulkUpsertRequest::GetProtoRequest(req);
}

class TUploadRowsRPCPublic : public NTxProxy::TUploadRowsBase<NKikimrServices::TActivity::GRPC_REQ> {
    using TBase = NTxProxy::TUploadRowsBase<NKikimrServices::TActivity::GRPC_REQ>;
public:
    explicit TUploadRowsRPCPublic(IRequestOpCtx* request, bool diskQuotaExceeded)
        : TBase(GetDuration(GetProtoRequest(request)->operation_params().operation_timeout()), diskQuotaExceeded)
        , Request(request)
    {}

private:
    static bool CellFromProtoVal(NScheme::TTypeInfo type, const Ydb::Value* vp,
                                  TCell& c, TString& err, TMemoryPool& valueDataPool)
    {
        if (vp->Hasnull_flag_value()) {
            c = TCell();
            return true;
        }

        if (vp->Hasnested_value()) {
            vp = &vp->Getnested_value();
        }

        const Ydb::Value& val = *vp;

#define EXTRACT_VAL(cellType, protoType, cppType) \
        case NScheme::NTypeIds::cellType : { \
                cppType v = val.Get##protoType##_value(); \
                c = TCell((const char*)&v, sizeof(v)); \
                break; \
            }

        switch (type.GetTypeId()) {
        EXTRACT_VAL(Bool, bool, ui8);
        EXTRACT_VAL(Int8, int32, i8);
        EXTRACT_VAL(Uint8, uint32, ui8);
        EXTRACT_VAL(Int16, int32, i16);
        EXTRACT_VAL(Uint16, uint32, ui16);
        EXTRACT_VAL(Int32, int32, i32);
        EXTRACT_VAL(Uint32, uint32, ui32);
        EXTRACT_VAL(Int64, int64, i64);
        EXTRACT_VAL(Uint64, uint64, ui64);
        EXTRACT_VAL(Float, float, float);
        EXTRACT_VAL(Double, double, double);
        EXTRACT_VAL(Date, uint32, ui16);
        EXTRACT_VAL(Datetime, uint32, ui32);
        EXTRACT_VAL(Timestamp, uint64, ui64);
        EXTRACT_VAL(Interval, int64, i64);
        case NScheme::NTypeIds::Json :
        case NScheme::NTypeIds::Utf8 : {
                TString v = val.Gettext_value();
                c = TCell(v.data(), v.size());
                break;
            }
        case NScheme::NTypeIds::JsonDocument : {
            const auto binaryJson = NBinaryJson::SerializeToBinaryJson(val.Gettext_value());
            if (!binaryJson.Defined()) {
                err = "Invalid JSON for JsonDocument provided";
                return false;
            }
            const auto binaryJsonInPool = valueDataPool.AppendString(TStringBuf(binaryJson->Data(), binaryJson->Size()));
            c = TCell(binaryJsonInPool.data(), binaryJsonInPool.size());
            break;
        }
        case NScheme::NTypeIds::DyNumber : {
            const auto dyNumber = NDyNumber::ParseDyNumberString(val.Gettext_value());
            if (!dyNumber.Defined()) {
                err = "Invalid DyNumber string representation";
                return false;
            }
            const auto dyNumberInPool = valueDataPool.AppendString(TStringBuf(*dyNumber));
            c = TCell(dyNumberInPool.data(), dyNumberInPool.size());
            break;
        }
        case NScheme::NTypeIds::Yson :
        case NScheme::NTypeIds::String : {
                TString v = val.Getbytes_value();
                c = TCell(v.data(), v.size());
                break;
            }
        case NScheme::NTypeIds::Decimal : {
            std::pair<ui64,ui64>& decimalVal = *valueDataPool.Allocate<std::pair<ui64,ui64> >();
            decimalVal.first = val.low_128();
            decimalVal.second = val.high_128();
            c = TCell((const char*)&decimalVal, sizeof(decimalVal));
            break;
        }
        case NScheme::NTypeIds::Pg : {
            TString v = val.Getbytes_value();
            c = TCell(v.data(), v.size());
            break;
        }
        default:
            err = Sprintf("Unexpected type %d", type.GetTypeId());
            return false;
        };

        return CheckValueData(type, c, err);
    }

    template <class TProto>
    static bool FillCellsFromProto(TVector<TCell>& cells, const TVector<TFieldDescription>& descr, const TProto& proto,
                                   TString& err, TMemoryPool& valueDataPool)
    {
        cells.clear();
        cells.reserve(descr.size());

        for (auto& fd : descr) {
            if (proto.items_size() <= (int)fd.PositionInStruct) {
                err = "Invalid request";
                return false;
            }
            cells.push_back({});
            if (!CellFromProtoVal(fd.Type, &proto.Getitems(fd.PositionInStruct), cells.back(), err, valueDataPool)) {
                return false;
            }

            if (fd.NotNull && cells.back().IsNull()) {
                err = TStringBuilder() << "Received NULL value for not null column: " << fd.ColName;
                return false;
            }
        }

        return true;
    }

private:
    bool ReportCostInfoEnabled() const {
        return GetProtoRequest(Request.get())->operation_params().report_cost_info() == Ydb::FeatureFlag::ENABLED;
    }

    TString GetDatabase()override {
        return Request->GetDatabaseName().GetOrElse(DatabaseFromDomain(AppData()));
    }

    const TString& GetTable() override {
        return GetProtoRequest(Request.get())->table();
    }

    const TVector<std::pair<TSerializedCellVec, TString>>& GetRows() const override {
        return AllRows;
    }

    void RaiseIssue(const NYql::TIssue& issue) override {
        return Request->RaiseIssue(issue);
    }

    void SendResult(const NActors::TActorContext&, const StatusIds::StatusCode& status) override {
        const Ydb::Table::BulkUpsertResult result;
        if (status == StatusIds::SUCCESS) {
            ui64 cost = std::ceil(RuCost);
            Request->SetRuHeader(cost);
            if (ReportCostInfoEnabled()) {
                Request->SetCostInfo(cost);
            }
        }
        return Request->SendResult(result, status);
    }

    bool CheckAccess(TString& errorMessage) override {
        if (Request->GetInternalToken().empty())
            return true;

        NACLib::TUserToken userToken(Request->GetInternalToken());
        const ui32 access = NACLib::EAccessRights::UpdateRow;
        auto resolveResult = GetResolveNameResult();
        if (!resolveResult) {
            TStringStream explanation;
            explanation << "Access denied for " << userToken.GetUserSID()
                        << " table '" << GetProtoRequest(Request.get())->table()
                        << "' has not been resolved yet";

            errorMessage = explanation.Str();
            return false;
        }
        for (const NSchemeCache::TSchemeCacheNavigate::TEntry& entry : resolveResult->ResultSet) {
            if (entry.Status == NSchemeCache::TSchemeCacheNavigate::EStatus::Ok
                && entry.SecurityObject != nullptr
                && !entry.SecurityObject->CheckAccess(access, userToken))
            {
                TStringStream explanation;
                explanation << "Access denied for " << userToken.GetUserSID()
                            << " with access " << NACLib::AccessRightsToString(access)
                            << " to table '" << GetProtoRequest(Request.get())->table() << "'";

                errorMessage = explanation.Str();
                return false;
            }
        }
        return true;
    }

    TVector<std::pair<TString, Ydb::Type>> GetRequestColumns(TString& errorMessage) const override {
        Y_UNUSED(errorMessage);

        const auto& type = GetProtoRequest(Request.get())->Getrows().Gettype();
        const auto& rowType = type.Getlist_type();
        const auto& rowFields = rowType.Getitem().Getstruct_type().Getmembers();

        TVector<std::pair<TString, Ydb::Type>> result;

        for (i32 pos = 0; pos < rowFields.size(); ++pos) {
            const auto& name = rowFields[pos].Getname();
            const auto& typeInProto = rowFields[pos].type().has_optional_type() ?
                        rowFields[pos].type().optional_type().item() : rowFields[pos].type();

            result.emplace_back(name, typeInProto);
        }
        return result;
    }

    bool ExtractRows(TString& errorMessage) override {
        // Parse type field
        // Check that it is a list of stuct
        // List all memebers and check their names and types
        // Save indexes of key column members and no-key members

        TVector<TCell> keyCells;
        TVector<TCell> valueCells;
        float cost = 0.0f;

        // TODO: check that value is a list of structs

        // For each row in values
        TMemoryPool valueDataPool(256);
        const auto& rows = GetProtoRequest(Request.get())->Getrows().Getvalue().Getitems();
        for (const auto& r : rows) {
            valueDataPool.Clear();

            ui64 sz = 0;
            // Take members corresponding to key columns
            if (!FillCellsFromProto(keyCells, KeyColumnPositions, r, errorMessage, valueDataPool)) {
                return false;
            }

            // Fill rest of cells with non-key column members
            if (!FillCellsFromProto(valueCells, ValueColumnPositions, r, errorMessage, valueDataPool)) {
                return false;
            }

            for (const auto& cell : keyCells) {
                sz += cell.Size();
            }

            for (const auto& cell : valueCells) {
                sz += cell.Size();
            }

            cost += TUpsertCost::OneRowCost(sz);

            // Save serialized key and value
            TSerializedCellVec serializedKey(TSerializedCellVec::Serialize(keyCells));
            TString serializedValue = TSerializedCellVec::Serialize(valueCells);
            AllRows.emplace_back(serializedKey, serializedValue);
        }

        RuCost = TUpsertCost::CostToRu(cost);
        return true;
    }

    bool ExtractBatch(TString& errorMessage) override {
        Batch = RowsToBatch(AllRows, errorMessage);
        return Batch.get();
    }

private:
    std::unique_ptr<IRequestOpCtx> Request;
    TVector<std::pair<TSerializedCellVec, TString>> AllRows;
};

class TUploadColumnsRPCPublic : public NTxProxy::TUploadRowsBase<NKikimrServices::TActivity::GRPC_REQ> {
    using TBase = NTxProxy::TUploadRowsBase<NKikimrServices::TActivity::GRPC_REQ>;
public:
    explicit TUploadColumnsRPCPublic(IRequestOpCtx* request, bool diskQuotaExceeded)
        : TBase(GetDuration(GetProtoRequest(request)->operation_params().operation_timeout()), diskQuotaExceeded)
        , Request(request)
    {}

private:
    bool ReportCostInfoEnabled() const {
        return GetProtoRequest(Request.get())->operation_params().report_cost_info() == Ydb::FeatureFlag::ENABLED;
    }

    EUploadSource GetSourceType() const override {
        auto* req = GetProtoRequest(Request.get());
        if (req->has_arrow_batch_settings()) {
            return EUploadSource::ArrowBatch;
        }
        if (req->has_csv_settings()) {
            return EUploadSource::CSV;
        }
        Y_VERIFY(false, "unexpected format");
    }

    TString GetDatabase() override {
        return Request->GetDatabaseName().GetOrElse(DatabaseFromDomain(AppData()));
    }

    const TString& GetTable() override {
        return GetProtoRequest(Request.get())->table();
    }

    const TVector<std::pair<TSerializedCellVec, TString>>& GetRows() const override {
        return Rows;
    }

    const TString& GetSourceData() const override {
        return GetProtoRequest(Request.get())->data();
    }

    const TString& GetSourceSchema() const override {
        static const TString none;
        if (GetProtoRequest(Request.get())->has_arrow_batch_settings()) {
            return GetProtoRequest(Request.get())->arrow_batch_settings().schema();
        }
        return none;
    }

    void RaiseIssue(const NYql::TIssue& issue) override {
        return Request->RaiseIssue(issue);
    }

    void SendResult(const NActors::TActorContext&, const StatusIds::StatusCode& status) override {
        const Ydb::Table::BulkUpsertResult result;
        if (status == StatusIds::SUCCESS) {
            ui64 cost = std::ceil(RuCost);
            Request->SetRuHeader(cost);
            if (ReportCostInfoEnabled()) {
                Request->SetCostInfo(cost);
            }
        }
        return Request->SendResult(result, status);
    }

    bool CheckAccess(TString& errorMessage) override {
        if (Request->GetInternalToken().empty())
            return true;

        NACLib::TUserToken userToken(Request->GetInternalToken());
        const ui32 access = NACLib::EAccessRights::UpdateRow;
        auto resolveResult = GetResolveNameResult();
        if (!resolveResult) {
            TStringStream explanation;
            explanation << "Access denied for " << userToken.GetUserSID()
                        << " table '" << GetProtoRequest(Request.get())->table()
                        << "' has not been resolved yet";

            errorMessage = explanation.Str();
            return false;
        }
        for (const NSchemeCache::TSchemeCacheNavigate::TEntry& entry : resolveResult->ResultSet) {
            if (entry.Status == NSchemeCache::TSchemeCacheNavigate::EStatus::Ok
                && entry.SecurityObject != nullptr
                && !entry.SecurityObject->CheckAccess(access, userToken))
            {
                TStringStream explanation;
                explanation << "Access denied for " << userToken.GetUserSID()
                            << " with access " << NACLib::AccessRightsToString(access)
                            << " to table '" << GetProtoRequest(Request.get())->table() << "'";

                errorMessage = explanation.Str();
                return false;
            }
        }
        return true;
    }

    TVector<std::pair<TString, Ydb::Type>> GetRequestColumns(TString& errorMessage) const override {
        if (GetSourceType() == EUploadSource::CSV) {
            // TODO: for CSV with header we have to extract columns from data (from first batch in file stream)
            return {};
        }

        auto schema = NArrow::DeserializeSchema(GetSourceSchema());
        if (!schema) {
            errorMessage = TString("Wrong schema in bulk upsert data");
            return {};
        }

        TVector<std::pair<TString, Ydb::Type>> out;
        out.reserve(schema->num_fields());

        for (auto& field : schema->fields()) {
            auto& name = field->name();
            auto& type = field->type();

            Ydb::Type ydbType;
            if (!ConvertArrowToYdbPrimitive(*type, ydbType)) {
                errorMessage = TString("Cannot convert arrow type to ydb one: " + type->ToString());
                return {};
            }
            out.emplace_back(name, std::move(ydbType));
        }

        return out;
    }

    bool ExtractRows(TString& errorMessage) override {
        Y_VERIFY(Batch);
        Rows = BatchToRows(Batch, errorMessage);
        return errorMessage.empty();
    }

    bool ExtractBatch(TString& errorMessage) override {
        switch (GetSourceType()) {
            case EUploadSource::ProtoValues:
            {
                errorMessage = "Unexpected data format in column upsert";
                return false;
            }
            case EUploadSource::ArrowBatch:
            {
                auto schema = NArrow::DeserializeSchema(GetSourceSchema());
                if (!schema) {
                    errorMessage = "Bad schema in bulk upsert data";
                    return false;
                }

                auto& data = GetSourceData();
                Batch = NArrow::DeserializeBatch(data, schema);
                if (!Batch) {
                    errorMessage = "Cannot deserialize arrow batch with specified schema";
                    return false;
                }
                break;
            }
            case EUploadSource::CSV:
            {
                if (SrcColumns.empty()) {
                    errorMessage = "Cannot upsert CSV: no src columns";
                    return false;
                }

                auto& data = GetSourceData();
                auto& cvsSettings = GetCsvSettings();
                ui32 skipRows = cvsSettings.skip_rows();
                auto& delimiter = cvsSettings.delimiter();
                auto& nullValue = cvsSettings.null_value();
                bool withHeader = cvsSettings.header();

                ui32 blockSize = NFormats::TArrowCSV::DEFAULT_BLOCK_SIZE;
                if (data.size() >= blockSize) {
                    blockSize *= data.size() / blockSize + 1;
                }
                NFormats::TArrowCSV reader(SrcColumns, skipRows, withHeader, blockSize);

                if (!delimiter.empty()) {
                    if (delimiter.size() != 1) {
                        errorMessage = TStringBuilder() << "Wrong delimiter '" << delimiter << "'";
                        return false;
                    }

                    reader.SetDelimiter(delimiter[0]);
                }

                if (!nullValue.empty()) {
                    reader.SetNullValue(nullValue);
                }

                Batch = reader.ReadNext(data, errorMessage);
                if (!Batch) {
                    if (errorMessage.empty()) {
                        errorMessage = "Cannot read CSV data";
                    }
                    return false;
                }

                if (reader.ReadNext(data, errorMessage)) {
                    errorMessage = "Too big CSV batch";
                    return false;
                }

                break;
            }
        }

        return true;
    }

private:
    std::unique_ptr<IRequestOpCtx> Request;
    TVector<std::pair<TSerializedCellVec, TString>> Rows;

    const Ydb::Formats::CsvSettings& GetCsvSettings() const {
        return GetProtoRequest(Request.get())->csv_settings();
    }
};

void DoBulkUpsertRequest(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider &) {
    bool diskQuotaExceeded = p->GetDiskQuotaExceeded();

    if (GetProtoRequest(p.get())->has_arrow_batch_settings()) {
        TActivationContext::AsActorContext().Register(new TUploadColumnsRPCPublic(p.release(), diskQuotaExceeded));
    } else if (GetProtoRequest(p.get())->has_csv_settings()) {
        TActivationContext::AsActorContext().Register(new TUploadColumnsRPCPublic(p.release(), diskQuotaExceeded));
    } else {
        TActivationContext::AsActorContext().Register(new TUploadRowsRPCPublic(p.release(), diskQuotaExceeded));
    }
}

template<>
IActor* TEvBulkUpsertRequest::CreateRpcActor(NKikimr::NGRpcService::IRequestOpCtx* msg) {
    bool diskQuotaExceeded = msg->GetDiskQuotaExceeded();

    if (GetProtoRequest(msg)->has_arrow_batch_settings()) {
        return new TUploadColumnsRPCPublic(msg, diskQuotaExceeded);
    } else if (GetProtoRequest(msg)->has_csv_settings()) {
        return new TUploadColumnsRPCPublic(msg, diskQuotaExceeded);
    } else {
        return new TUploadRowsRPCPublic(msg, diskQuotaExceeded);
    }
}


} // namespace NKikimr
} // namespace NGRpcService
