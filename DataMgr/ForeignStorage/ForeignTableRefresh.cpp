/*
 * Copyright 2020 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ForeignTableRefresh.h"
#include "Catalog/Catalog.h"
#include "LockMgr/LockMgr.h"

#include "QueryEngine/ExternalCacheInvalidators.h"

namespace foreign_storage {
void refresh_foreign_table_unlocked(Catalog_Namespace::Catalog& catalog,
                                    const ForeignTable& td,
                                    const bool evict_cached_entries) {
  auto& data_mgr = catalog.getDataMgr();
  ChunkKey table_key{catalog.getCurrentDB().dbId, td.tableId};
  ResultSetCacheInvalidator::invalidateCachesByTable(boost::hash_value(table_key));

  catalog.removeFragmenterForTable(td.tableId);

  if (catalog.getForeignTable(td.tableId)->isAppendMode() && !evict_cached_entries) {
    ChunkMetadataVector metadata_vec;
    data_mgr.getChunkMetadataVecForKeyPrefix(metadata_vec, table_key);
    int last_fragment_id = 0;
    for (const auto& [key, metadata] : metadata_vec) {
      if (key[CHUNK_KEY_FRAGMENT_IDX] > last_fragment_id) {
        last_fragment_id = key[CHUNK_KEY_FRAGMENT_IDX];
      }
    }
    for (const auto& [key, metadata] : metadata_vec) {
      if (key[CHUNK_KEY_FRAGMENT_IDX] == last_fragment_id) {
        data_mgr.deleteChunksWithPrefix(key, MemoryLevel::CPU_LEVEL);
        data_mgr.deleteChunksWithPrefix(key, MemoryLevel::GPU_LEVEL);
      }
    }
  } else {
    data_mgr.deleteChunksWithPrefix(table_key, MemoryLevel::CPU_LEVEL);
    data_mgr.deleteChunksWithPrefix(table_key, MemoryLevel::GPU_LEVEL);
  }

  try {
    data_mgr.getPersistentStorageMgr()->getForeignStorageMgr()->refreshTable(
        table_key, evict_cached_entries);
    catalog.updateForeignTableRefreshTimes(td.tableId);
  } catch (PostEvictionRefreshException& e) {
    catalog.updateForeignTableRefreshTimes(td.tableId);
    throw e.getOriginalException();
  }
}

void refresh_foreign_table(Catalog_Namespace::Catalog& catalog,
                           const std::string& table_name,
                           const bool evict_cached_entries) {
  auto table_lock =
      std::make_unique<lockmgr::TableSchemaLockContainer<lockmgr::WriteLock>>(
          lockmgr::TableSchemaLockContainer<lockmgr::WriteLock>::acquireTableDescriptor(
              catalog, table_name, false));

  const TableDescriptor* td = (*table_lock)();
  if (td->storageType != StorageType::FOREIGN_TABLE) {
    throw std::runtime_error{
        table_name +
        " is not a foreign table. Refreshes are applicable to only foreign tables."};
  }

  auto foreign_table = dynamic_cast<const ForeignTable*>(td);
  CHECK(foreign_table);
  refresh_foreign_table_unlocked(catalog, *foreign_table, evict_cached_entries);
}
}  // namespace foreign_storage
