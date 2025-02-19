#include "defs.h"
#include "db_wrapper.h"
#include <ydb/core/tx/columnshard/columnshard_schema.h>

namespace NKikimr::NOlap {

void TDbWrapper::Insert(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_Insert(db, data);
}

void TDbWrapper::Commit(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_Commit(db, data);
}

void TDbWrapper::Abort(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_Abort(db, data);
}

void TDbWrapper::EraseInserted(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_EraseInserted(db, data);
}

void TDbWrapper::EraseCommitted(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_EraseCommitted(db, data);
}

void TDbWrapper::EraseAborted(const TInsertedData& data) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::InsertTable_EraseAborted(db, data);
}

bool TDbWrapper::Load(THashMap<TWriteId, TInsertedData>& inserted,
                      THashMap<ui64, TSet<TInsertedData>>& committed,
                      THashMap<TWriteId, TInsertedData>& aborted,
                      const TInstant& loadTime) {
    NIceDb::TNiceDb db(Database);
    return NColumnShard::Schema::InsertTable_Load(db, DsGroupSelector, inserted, committed, aborted, loadTime);
}

void TDbWrapper::WriteGranule(ui32 index, const IColumnEngine& engine, const TGranuleRecord& row) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::IndexGranules_Write(db, index, engine, row);
}

void TDbWrapper::EraseGranule(ui32 index, const IColumnEngine& engine, const TGranuleRecord& row) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::IndexGranules_Erase(db, index, engine, row);
}

bool TDbWrapper::LoadGranules(ui32 index, const IColumnEngine& engine, std::function<void(TGranuleRecord&&)> callback) {
    NIceDb::TNiceDb db(Database);
    return NColumnShard::Schema::IndexGranules_Load(db, index, engine, callback);
}

void TDbWrapper::WriteColumn(ui32 index, const TColumnRecord& row) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::IndexColumns_Write(db, index, row);
}

void TDbWrapper::EraseColumn(ui32 index, const TColumnRecord& row) {
    NIceDb::TNiceDb db(Database);
    NColumnShard::Schema::IndexColumns_Erase(db, index, row);
}

bool TDbWrapper::LoadColumns(ui32 index, std::function<void(TColumnRecord&&)> callback) {
    NIceDb::TNiceDb db(Database);
    return NColumnShard::Schema::IndexColumns_Load(db, DsGroupSelector, index, callback);
}

void TDbWrapper::WriteCounter(ui32 index, ui32 counterId, ui64 value) {
    NIceDb::TNiceDb db(Database);
    return NColumnShard::Schema::IndexCounters_Write(db, index, counterId, value);
}

bool TDbWrapper::LoadCounters(ui32 index, std::function<void(ui32 id, ui64 value)> callback) {
    NIceDb::TNiceDb db(Database);
    return NColumnShard::Schema::IndexCounters_Load(db, index, callback);
}

}
