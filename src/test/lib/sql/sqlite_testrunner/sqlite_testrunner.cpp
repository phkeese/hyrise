#include "sqlite_testrunner.hpp"

namespace hyrise {

void SQLiteTestRunner::SetUpTestSuite() {
  /**
   * This loads the tables used for the SQLiteTestRunner into the Hyrise cache
   * (_table_cache_per_encoding[EncodingType::Unencoded]) and into SQLite.
   * Later, when running the individual queries, we only reload tables from disk if they have been modified by the
   * previous query.
   */

  _sqlite = std::make_unique<SQLiteWrapper>();

  auto unencoded_table_cache = TableCache{};

  auto file = std::ifstream{"resources/test_data/sqlite_testrunner.tables"};
  auto line = std::string{};
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }

    auto args = std::vector<std::string>{};
    boost::algorithm::split(args, line, boost::is_space());

    if (args.size() != 2) {
      continue;
    }

    const auto table_file = args.at(0);
    const auto table_name = args.at(1);

    const auto table = load_table(table_file, CHUNK_SIZE);

    // Store loaded tables in a map that basically caches the loaded tables. In case the table
    // needs to be reloaded (e.g., due to modifications), we also store the file path.
    unencoded_table_cache.emplace(table_name, TableCacheEntry{table, table_file});

    // Create test table and also table copy which is later used as the master to copy from.
    _sqlite->create_sqlite_table(*table, table_name);
    _sqlite->create_sqlite_table(*table, table_name + _master_table_suffix);
  }

  _table_cache_per_encoding.emplace(EncodingType::Unencoded, unencoded_table_cache);

  _lqp_cache = std::make_shared<SQLLogicalPlanCache>();
  _pqp_cache = std::make_shared<SQLPhysicalPlanCache>();

  // DO NOT modify the Hyrise class here, as those changes will get overwritten by the base test. Instead, make those
  // changes in SetUp().
}

void SQLiteTestRunner::TearDownTestSuite() {
  _sqlite.reset();
  _table_cache_per_encoding.clear();
  _lqp_cache.reset();
  _pqp_cache.reset();
  _sqlite_result_cache.clear();
}

void SQLiteTestRunner::SetUp() {
  const auto& param = GetParam();

  // Enable multi-threading for SQLite test runner
  Hyrise::get().topology.use_numa_topology();
  Hyrise::get().set_scheduler(std::make_shared<NodeQueueScheduler>());

  /**
   * Encode Tables if no encoded variant of a Table is in the cache
   */
  const auto encoding_type = std::get<1>(param);
  auto table_cache_iter = _table_cache_per_encoding.find(encoding_type);

  if (table_cache_iter == _table_cache_per_encoding.end()) {
    const auto& unencoded_table_cache = _table_cache_per_encoding.at(EncodingType::Unencoded);
    auto encoded_table_cache = TableCache{};

    for (auto const& [table_name, table_cache_entry] : unencoded_table_cache) {
      auto table = load_table(table_cache_entry.filename, CHUNK_SIZE);

      auto chunk_encoding_spec = create_compatible_chunk_encoding_spec(*table, SegmentEncodingSpec{encoding_type});
      ChunkEncoder::encode_all_chunks(table, chunk_encoding_spec);

      encoded_table_cache.emplace(table_name,
                                  TableCacheEntry{table, table_cache_entry.filename, chunk_encoding_spec, false});
    }

    table_cache_iter = _table_cache_per_encoding.emplace(encoding_type, encoded_table_cache).first;
  }

  auto& table_cache = table_cache_iter->second;

  // In case the previous SQL query was not executed successfully, we always reset all tables
  // because we cannot be sure that the dirty flags have been properly set.
  if (!_last_run_successful) {
    for (auto& [_, table_cache_entry] : table_cache) {
      table_cache_entry.dirty = true;
    }
  }

  /**
   * Reset dirty tables in SQLite
   */
  for (const auto& table_name_and_cache : table_cache) {
    const auto& table_name = table_name_and_cache.first;

    // When tables in Hyrise were (potentially) modified, we assume the same happened in sqlite.
    // The SQLite table is considered dirty if any of its encoded versions in hyrise are dirty.
    const auto sqlite_table_dirty = std::any_of(
        _table_cache_per_encoding.begin(), _table_cache_per_encoding.end(), [&](const auto& table_cache_for_encoding) {
          const auto& table_cache_entry = table_cache_for_encoding.second.at(table_name);
          return table_cache_entry.dirty;
        });

    if (sqlite_table_dirty) {
      _sqlite->reset_table_from_copy(table_name, table_name + _master_table_suffix);
    }
  }

  /**
   * Populate the StorageManager with mint Tables with the correct encoding from the cache
   */
  for (auto& [table_name, table_cache_entry] : table_cache) {
    /*
      Hyrise:
        We start off with cached tables (SetUpTestSuite) and add them to the resetted
        storage manager before each test here. In case tables have been modified, they are
        removed from the cache and we thus need to reload them from the initial tbl file.
      SQLite:
        Drop table and copy the whole table from the master table to reset all accessed tables.
    */
    if (table_cache_entry.dirty) {
      // 1. reload table from tbl file, 2. add table to storage manager, 3. cache table in map
      auto reloaded_table = load_table(table_cache_entry.filename, CHUNK_SIZE);
      if (encoding_type != EncodingType::Unencoded) {
        // Do not call ChunkEncoder when in Unencoded mode since the ChunkEncoder will also generate
        // pruning statistics and we want to run this test without them as well, so we hijack the Unencoded
        // mode for this.
        // TODO(anybody) Extract pruning statistics generation from ChunkEncoder
        ChunkEncoder::encode_all_chunks(reloaded_table, table_cache_entry.chunk_encoding_spec);
      }

      Hyrise::get().storage_manager.add_table(table_name, reloaded_table);
      table_cache_entry.table = reloaded_table;

    } else {
      Hyrise::get().storage_manager.add_table(table_name, table_cache_entry.table);
    }
  }
}

std::vector<std::pair<size_t, std::string>> SQLiteTestRunner::queries() {
  static auto queries = std::vector<std::pair<size_t, std::string>>{};

  if (!queries.empty()) {
    return queries;
  }

  auto file = std::ifstream{"resources/test_data/sqlite_testrunner_queries.sql"};
  auto query = std::string{};

  auto next_line = size_t{0};  // Incremented before first use
  while (std::getline(file, query)) {
    ++next_line;
    if (query.empty() || query.substr(0, 2) == "--") {
      continue;
    }

    queries.emplace_back(next_line, std::move(query));
  }

  return queries;
}

TEST_P(SQLiteTestRunner, CompareToSQLite) {
  _last_run_successful = false;

  const auto [query_pair, encoding_type] = GetParam();
  const auto& [line, sql] = query_pair;

  SCOPED_TRACE("Query '" + sql + "' from line " + std::to_string(line) + " with encoding " +
               std::string{magic_enum::enum_name(encoding_type)});

  // Execute query in Hyrise.
  auto sql_pipeline = SQLPipelineBuilder{sql}.with_pqp_cache(_pqp_cache).with_lqp_cache(_lqp_cache).create_pipeline();
  const auto [pipeline_status, result_table] = sql_pipeline.get_result_table();
  ASSERT_EQ(pipeline_status, SQLPipelineStatus::Success);

  // Obtain SQLite result from cache or execute the query.
  auto sqlite_result_table = std::shared_ptr<const Table>{};
  if (const auto cached_sqlite_result = _sqlite_result_cache.try_get(sql)) {
    sqlite_result_table = *cached_sqlite_result;
  } else {
    sqlite_result_table = _sqlite->main_connection.execute_query(sql);
    _sqlite_result_cache.set(sql, sqlite_result_table);
  }

  ASSERT_TRUE(result_table && result_table->row_count() > 0 && sqlite_result_table &&
              sqlite_result_table->row_count() > 0)
      << "The SQLiteTestRunner cannot handle queries without results. We can only infer column types from sqlite if "
         "they have at least one row";

  auto order_sensitivity = OrderSensitivity::No;
  const auto& parse_result = sql_pipeline.get_parsed_sql_statements().back();
  if (parse_result->getStatements().front()->is(hsql::kStmtSelect)) {
    auto select_statement = dynamic_cast<const hsql::SelectStatement*>(parse_result->getStatements().back());
    if (select_statement->order) {
      order_sensitivity = OrderSensitivity::Yes;
    }
  }

  const auto table_comparison_msg =
      check_table_equal(result_table, sqlite_result_table, order_sensitivity, TypeCmpMode::Lenient,
                        FloatComparisonMode::RelativeDifference, IgnoreNullable::Yes);

  if (table_comparison_msg) {
    FAIL() << "Query failed: " << *table_comparison_msg << '\n';
  }

  // Mark Tables modified by the query as dirty
  for (const auto& plan : sql_pipeline.get_optimized_logical_plans()) {
    for (const auto& table_name : lqp_find_modified_tables(plan)) {
      if (!_table_cache_per_encoding.at(encoding_type).contains(table_name)) {
        // Table was not cached, for example because it was created as part of the query
        continue;
      }
      // mark table cache entry as dirty, when table has been modified
      _table_cache_per_encoding.at(encoding_type).at(table_name).dirty = true;
    }
  }

  // Delete newly created views in sqlite
  for (const auto& plan : sql_pipeline.get_optimized_logical_plans()) {
    if (const auto create_view = std::dynamic_pointer_cast<CreateViewNode>(plan)) {
      _sqlite->main_connection.execute_query("DROP VIEW IF EXISTS " + create_view->view_name + ";");
    }
  }

  _last_run_successful = true;
}

}  // namespace hyrise
