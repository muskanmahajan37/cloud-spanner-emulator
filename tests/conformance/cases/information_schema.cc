//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "zetasql/base/statusor.h"
#include "tests/conformance/common/database_test_base.h"

namespace google {
namespace spanner {
namespace emulator {
namespace test {

namespace {

class InformationSchemaTest : public DatabaseTest {
 public:
  absl::Status SetUpDatabase() override {
    return SetSchema({R"(
      CREATE TABLE Base (
        Key1 INT64,
        Key2 STRING(256),
        BoolValue BOOL,
        IntValue INT64 NOT NULL,
        DoubleValue FLOAT64,
        StrValue STRING(MAX),
        ByteValue BYTES(256),
        TimestampValue TIMESTAMP options (allow_commit_timestamp = true),
        DateValue DATE,
        BoolArray ARRAY<BOOL> NOT NULL,
        IntArray ARRAY<INT64>,
        DoubleArray ARRAY<FLOAT64>,
        StrArray ARRAY<STRING(256)>,
        ByteArray ARRAY<BYTES(MAX)>,
        TimestampArray ARRAY<TIMESTAMP>,
        DateArray ARRAY<DATE>
      ) PRIMARY KEY (Key1, Key2 DESC)
    )",
                      R"(
      CREATE TABLE CascadeChild (
        Key1 INT64,
        Key2 STRING(256),
        ChildKey BOOL,
        Value1 STRING(MAX) NOT NULL,
        Value2 BOOL
      ) PRIMARY KEY (Key1, Key2 DESC, ChildKey ASC),
        INTERLEAVE IN PARENT Base ON DELETE CASCADE
    )",
                      R"(
      CREATE TABLE NoActionChild (
        Key1 INT64,
        Key2 STRING(256),
        ChildKey BOOL,
        Value  STRING(MAX)
      ) PRIMARY KEY (Key1, Key2 DESC, ChildKey ASC),
        INTERLEAVE IN PARENT Base ON DELETE NO ACTION
    )",
                      R"(
      CREATE UNIQUE NULL_FILTERED INDEX CascadeChildByValue
      ON CascadeChild(Key1, Key2 DESC, Value2 ASC)
      STORING(Value1), INTERLEAVE IN Base
    )",
                      R"(
      CREATE INDEX NoActionChildByValue ON NoActionChild(Value ASC)
    )",
                      R"(
      ALTER TABLE Base ADD CONSTRAINT FKBaseCascadeChild
          FOREIGN KEY(BoolValue, Key2)
          REFERENCES CascadeChild(ChildKey, Value1)
    )"});
  }

  // Information schema tables not yet supported.
  const std::pair<std::string, Value> kUnsupportedTables{
      "unsupported_tables", std::vector<std::string>({
                                "CHECK_CONSTRAINTS",
                                "DATABASE_OPTIONS",
                            })};

  // Information schema columns not yet supported.
  const std::pair<std::string, Value> kUnsupportedColumns{
      "unsupported_columns", std::vector<std::string>({
                                 "GENERATION_EXPRESSION",
                                 "IS_GENERATED",
                                 "IS_STORED",
                             })};

  // Information schema constraints not yet supported.
  const std::pair<std::string, Value> kUnsupportedConstraints{
      "unsupported_constraints",
      std::vector<std::string>({
          "CK_IS_NOT_NULL_COLUMNS_GENERATION_EXPRESSION",
          "CK_IS_NOT_NULL_COLUMNS_IS_GENERATED",
      })};

  // Returns the given rows, replacing matching string patterns with their
  // actual values from the given results.
  static std::vector<ValueRow> ExpectedRows(
      const zetasql_base::StatusOr<std::vector<ValueRow>>& results,
      const std::vector<ValueRow> rows) {
    if (!results.ok()) {
      return rows;
    }
    std::vector<ValueRow> expected;
    for (const ValueRow& row : rows) {
      ValueRow next;
      for (int i = 0; i < row.values().size(); ++i) {
        Value value = row.values()[i];
        if (value.get<std::string>().ok()) {
          std::string pattern = value.get<std::string>().value();
          value = Value(FindString(results, i, pattern));
        }
        next.add(value);
      }
      expected.push_back(next);
    }
    return expected;
  }

  // Returns the first result string that matches a pattern. Returns the pattern
  // if none match. One use case is to match generated names that have
  // different signatures between production and emulator.
  static std::string FindString(
      const zetasql_base::StatusOr<std::vector<ValueRow>>& results, int field_index,
      const std::string& pattern) {
    for (const auto& row : results.value()) {
      auto value = row.values()[field_index].get<std::string>().value();
      if (RE2::FullMatch(value, pattern)) {
        return value;
      }
    }
    return pattern;
  }

  static void LogResults(const zetasql_base::StatusOr<std::vector<ValueRow>>& results) {
  }

  // Aliases so test expectations read more clearly.
  cloud::spanner::Value Nb() { return Null<Bytes>(); }
  cloud::spanner::Value Ns() { return Null<std::string>(); }
  cloud::spanner::Value Ni() { return Null<std::int64_t>(); }
};

TEST_F(InformationSchemaTest, Schemata) {
  auto results = Query(R"(
      select
        s.catalog_name,
        s.schema_name
      from
        information_schema.schemata AS s
      order by
        s.catalog_name,
        s.schema_name
      limit 2
    )");
  LogResults(results);
  auto expected = std::vector<ValueRow>({{"", ""}, {"", "INFORMATION_SCHEMA"}});
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaTables) {
  // The documented set of tables that should be returned is at:
  // https://cloud.google.com/spanner/docs/information-schema#information_schemadatabase_options.
  //
  // The tables filtered out by the WHERE clause are not currently available in
  // the emulator. This test should not need to filter on table_name.
  auto results = QueryWithParams(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.parent_table_name,
        t.on_delete_action,
        t.spanner_state
      from
        information_schema.tables AS t
      where
        t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
      order by
        t.table_name
    )",
                                 {kUnsupportedTables});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "COLUMNS", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "INDEXES", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "SCHEMATA", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "TABLES", Ns(), Ns(), Ns()},
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", Ns(), Ns(), Ns()},
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaColumns) {
  // The tables and columns filtered out by the WHERE clause are not currently
  // available in the emulator. This test should not need to filter on
  // table_name.
  //
  // This test currently ignores the ORDINAL_POSITION column as the emulator
  // reports a different value because production has additional columns that
  // the emulator does not yet support.
  auto results = QueryWithParams(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.column_default,
        t.data_type,
        t.is_nullable,
        t.spanner_type,
        t.spanner_state
      from
        information_schema.columns as t
      where
        t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
        and not (t.table_name = 'COLUMNS' and t.column_name in unnest(@unsupported_columns))
        and not (t.table_name = 'SCHEMATA' and t.column_name = 'EFFECTIVE_TIMESTAMP')
      order by
        t.table_name,
        t.column_name
    )",
                                 {kUnsupportedTables, kUnsupportedColumns});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "COLUMNS", "COLUMN_DEFAULT", Nb(), Ns(), "YES", "BYTES(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "COLUMN_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "DATA_TYPE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "IS_NULLABLE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "ORDINAL_POSITION", Nb(), Ns(), "NO", "INT64", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "SPANNER_STATE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "SPANNER_TYPE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "COLUMN_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_TYPE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_VALUE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "COLUMN_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_STATE", Nb(), Ns(), "NO", "STRING(100)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_TYPE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "IS_NULL_FILTERED", Nb(), Ns(), "NO", "BOOL", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "IS_UNIQUE", Nb(), Ns(), "NO", "BOOL", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "PARENT_TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "SPANNER_IS_MANAGED", Nb(), Ns(), "NO", "BOOL", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "COLUMN_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "COLUMN_ORDERING", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_TYPE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "IS_NULLABLE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "ORDINAL_POSITION", Nb(), Ns(), "YES", "INT64", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "SPANNER_TYPE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "COLUMN_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "ORDINAL_POSITION", Nb(), Ns(), "NO", "INT64", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "POSITION_IN_UNIQUE_CONSTRAINT", Nb(), Ns(), "YES", "INT64", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "DELETE_RULE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "MATCH_OPTION", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "SPANNER_STATE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "UNIQUE_CONSTRAINT_CATALOG", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "UNIQUE_CONSTRAINT_NAME", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "UNIQUE_CONSTRAINT_SCHEMA", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "UPDATE_RULE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "CATALOG_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "SCHEMA_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "ON_DELETE_ACTION", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "PARENT_TABLE_NAME", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "SPANNER_STATE", Nb(), Ns(), "YES", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_TYPE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "ENFORCED", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "INITIALLY_DEFERRED", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "IS_DEFERRABLE", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_CATALOG", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_NAME", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_SCHEMA", Nb(), Ns(), "NO", "STRING(MAX)", Ns()},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaIndexes) {
  auto results = QueryWithParams(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.index_name,
        t.index_type,
        t.parent_table_name,
        t.is_unique,
        t.is_null_filtered,
        t.index_state
      from
        information_schema.indexes as t
      where
        t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
      order by
        t.table_name,
        t.index_name
    )",
                                 {kUnsupportedTables});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns()},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaIndexColumns) {
  auto results = QueryWithParams(R"(
      select
        t.table_schema,
        t.table_name,
        t.index_name,
        t.index_type,
        t.column_name,
        t.ordinal_position,
        t.column_ordering,
        t.is_nullable,
        t.spanner_type
      from
        information_schema.index_columns as t
      where
        t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
      order by
        t.table_name,
        t.index_name,
        t.ordinal_position
    )",
                                 {kUnsupportedTables});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"INFORMATION_SCHEMA", "COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "COLUMN_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "COLUMN_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY_KEY", "PRIMARY_KEY", "OPTION_NAME", 5, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "COLUMN_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_CATALOG", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_SCHEMA", 5, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_NAME", 6, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "INDEX_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEXES", "PRIMARY_KEY", "PRIMARY_KEY", "INDEX_TYPE", 5, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "INDEX_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "INDEX_TYPE", 5, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY_KEY", "PRIMARY_KEY", "COLUMN_NAME", 6, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY_KEY", "PRIMARY_KEY", "COLUMN_NAME", 4, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "SCHEMATA", "PRIMARY_KEY", "PRIMARY_KEY", "CATALOG_NAME", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "SCHEMATA", "PRIMARY_KEY", "PRIMARY_KEY", "SCHEMA_NAME", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLES", "PRIMARY_KEY", "PRIMARY_KEY", "TABLE_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_CATALOG", 1, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_SCHEMA", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "PRIMARY_KEY", "PRIMARY_KEY", "CONSTRAINT_NAME", 3, "ASC", "NO", "STRING(MAX)"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaTableConstraints) {
  auto results = QueryWithParams(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.constraint_type,
        t.is_deferrable,
        t.initially_deferred,
        t.enforced
      from
        information_schema.table_constraints as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
        and t.constraint_name not in unnest(@unsupported_constraints)
      order by
        t.constraint_name
  )",
                                 {kUnsupportedTables, kUnsupportedConstraints});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_COLUMN_NAME", "", "INFORMATION_SCHEMA", "COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_ORDINAL_POSITION", "", "INFORMATION_SCHEMA", "COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_NAME", "", "INFORMATION_SCHEMA", "COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_COLUMN_NAME", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_NAME", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_TYPE", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_VALUE", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_NAME", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_COLUMN_NAME", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_NAME", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_NAME", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_NAME", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_STATE", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_TYPE", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_NULL_FILTERED", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_UNIQUE", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_PARENT_TABLE_NAME", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_SPANNER_IS_MANAGED", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_NAME", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "INDEXES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_COLUMN_NAME", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_NAME", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_TYPE", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_NAME", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_COLUMN_NAME", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_ORDINAL_POSITION", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_NAME", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_DELETE_RULE", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_MATCH_OPTION", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_SPANNER_STATE", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_UPDATE_RULE", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_CATALOG_NAME", "", "INFORMATION_SCHEMA", "SCHEMATA", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_SCHEMA_NAME", "", "INFORMATION_SCHEMA", "SCHEMATA", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "TABLES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_NAME", "", "INFORMATION_SCHEMA", "TABLES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "TABLES", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_TYPE", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_ENFORCED", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_INITIALLY_DEFERRED", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_IS_DEFERRABLE", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_CATALOG", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_NAME", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMNS", "", "INFORMATION_SCHEMA", "COLUMNS", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_SCHEMATA", "", "INFORMATION_SCHEMA", "SCHEMATA", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLES", "", "INFORMATION_SCHEMA", "TABLES", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaConstraintTableUsage) {
  auto results = QueryWithParams(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name
      from
        information_schema.constraint_table_usage as t
      where
        t.table_catalog = ''
        and t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
        and t.constraint_name not in unnest(@unsupported_constraints)
      order by
        t.table_name,
        t.constraint_name
    )",
                                 {kUnsupportedTables, kUnsupportedConstraints});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_ORDINAL_POSITION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "", "INFORMATION_SCHEMA", "PK_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_VALUE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_STATE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_NULL_FILTERED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_UNIQUE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_PARENT_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_SPANNER_IS_MANAGED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_ORDINAL_POSITION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_DELETE_RULE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_MATCH_OPTION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_SPANNER_STATE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_UPDATE_RULE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_CATALOG_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_SCHEMA_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "", "INFORMATION_SCHEMA", "PK_SCHEMATA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "", "INFORMATION_SCHEMA", "PK_TABLES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_ENFORCED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_INITIALLY_DEFERRED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_IS_DEFERRABLE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaReferentialConstraints) {
  auto results = Query(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.unique_constraint_catalog,
        t.unique_constraint_schema,
        t.unique_constraint_name,
        t.match_option,
        t.update_rule,
        t.delete_rule,
        t.spanner_state
      from
        information_schema.referential_constraints as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = 'INFORMATION_SCHEMA'
    )");
  EXPECT_THAT(results, IsOkAndHoldsRows({}));
}

TEST_F(InformationSchemaTest, MetaKeyColumnUsage) {
  auto results = QueryWithParams(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.ordinal_position,
        t.position_in_unique_constraint
      from
        information_schema.key_column_usage as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
      order by
        t.constraint_name,
        t.table_name,
        t.ordinal_position
    )",
                                 {kUnsupportedTables});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "PK_COLUMNS", "", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMNS", "", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMNS", "", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMNS", "", "INFORMATION_SCHEMA", "COLUMNS", "COLUMN_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "COLUMN_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS", "", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_NAME", 5, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "COLUMN_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_CATALOG", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_SCHEMA", 5, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE", "", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_NAME", 6, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "INDEX_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEXES", "", "INFORMATION_SCHEMA", "INDEXES", "INDEX_TYPE", 5, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_TYPE", 5, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS", "", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "COLUMN_NAME", 6, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE", "", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "COLUMN_NAME", 4, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS", "", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_SCHEMATA", "", "INFORMATION_SCHEMA", "SCHEMATA", "CATALOG_NAME", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_SCHEMATA", "", "INFORMATION_SCHEMA", "SCHEMATA", "SCHEMA_NAME", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLES", "", "INFORMATION_SCHEMA", "TABLES", "TABLE_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLES", "", "INFORMATION_SCHEMA", "TABLES", "TABLE_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLES", "", "INFORMATION_SCHEMA", "TABLES", "TABLE_NAME", 3, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_CATALOG", 1, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_SCHEMA", 2, Ni()},  // NOLINT
    {"", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS", "", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_NAME", 3, Ni()},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, MetaConstraintColumnUsage) {
  auto results = QueryWithParams(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name
      from
        information_schema.constraint_column_usage as t
      where
        t.table_catalog = ''
        and t.table_schema = 'INFORMATION_SCHEMA'
        and t.table_name not in unnest(@unsupported_tables)
        and t.constraint_name not in unnest(@unsupported_constraints)
      order by
        t.table_name,
        t.column_name,
        t.constraint_name
    )",
                                 {kUnsupportedTables, kUnsupportedConstraints});
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "INFORMATION_SCHEMA", "COLUMNS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "PK_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "ORDINAL_POSITION", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_ORDINAL_POSITION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMNS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMNS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_NAME", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_TYPE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "OPTION_VALUE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_OPTION_VALUE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_COLUMN_OPTIONS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "COLUMN_OPTIONS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_COLUMN_OPTIONS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_COLUMN_USAGE", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_COLUMN_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_CONSTRAINT_TABLE_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "CONSTRAINT_TABLE_USAGE", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_CONSTRAINT_TABLE_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_NAME", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_STATE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_STATE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_TYPE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_INDEX_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "INDEX_TYPE", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "IS_NULL_FILTERED", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_NULL_FILTERED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "IS_UNIQUE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_IS_UNIQUE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "PARENT_TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_PARENT_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "SPANNER_IS_MANAGED", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_SPANNER_IS_MANAGED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEXES_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEXES", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_INDEXES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_NAME", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_TYPE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_INDEX_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "INDEX_TYPE", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_INDEX_COLUMNS_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "INDEX_COLUMNS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_INDEX_COLUMNS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_COLUMN_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "COLUMN_NAME", "", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "PK_KEY_COLUMN_USAGE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "ORDINAL_POSITION", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_ORDINAL_POSITION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "KEY_COLUMN_USAGE", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_KEY_COLUMN_USAGE_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "PK_REFERENTIAL_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "DELETE_RULE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_DELETE_RULE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "MATCH_OPTION", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_MATCH_OPTION"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "SPANNER_STATE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_SPANNER_STATE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "REFERENTIAL_CONSTRAINTS", "UPDATE_RULE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_REFERENTIAL_CONSTRAINTS_UPDATE_RULE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "CATALOG_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_CATALOG_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "CATALOG_NAME", "", "INFORMATION_SCHEMA", "PK_SCHEMATA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "SCHEMA_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_SCHEMATA_SCHEMA_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "SCHEMATA", "SCHEMA_NAME", "", "INFORMATION_SCHEMA", "PK_SCHEMATA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "PK_TABLES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_NAME", "", "INFORMATION_SCHEMA", "PK_TABLES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLES_TABLE_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLES", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "PK_TABLES"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_CATALOG", "", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_NAME", "", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_SCHEMA"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_SCHEMA", "", "INFORMATION_SCHEMA", "PK_TABLE_CONSTRAINTS"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "CONSTRAINT_TYPE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_CONSTRAINT_TYPE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "ENFORCED", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_ENFORCED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "INITIALLY_DEFERRED", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_INITIALLY_DEFERRED"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "IS_DEFERRABLE", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_IS_DEFERRABLE"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_CATALOG", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_CATALOG"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_NAME", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_NAME"},  // NOLINT
    {"", "INFORMATION_SCHEMA", "TABLE_CONSTRAINTS", "TABLE_SCHEMA", "", "INFORMATION_SCHEMA", "CK_IS_NOT_NULL_TABLE_CONSTRAINTS_TABLE_SCHEMA"},  // NOLINT
  });
  // clang-format off
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultTables) {
  auto results = Query(R"(
      select
        t.table_name,
        t.parent_table_name,
        t.on_delete_action
      from
        information_schema.tables AS t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_catalog,
        t.table_schema,
        t.table_name
    )");
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"Base", Ns(), Ns()},
    {"CascadeChild", "Base", "CASCADE"},
    {"NoActionChild", "Base", "NO ACTION"}
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultColumns) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.ordinal_position,
        t.column_default,
        t.data_type,
        t.is_nullable,
        t.spanner_type
      from
        information_schema.columns AS t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.ordinal_position
    )");
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "", "Base", "Key1", 1, Nb(), Ns(), "YES", "INT64"},  // NOLINT
    {"", "", "Base", "Key2", 2, Nb(), Ns(), "YES", "STRING(256)"},  // NOLINT
    {"", "", "Base", "BoolValue", 3, Nb(), Ns(), "YES", "BOOL"},  // NOLINT
    {"", "", "Base", "IntValue", 4, Nb(), Ns(), "NO", "INT64"},  // NOLINT
    {"", "", "Base", "DoubleValue", 5, Nb(), Ns(), "YES", "FLOAT64"},  // NOLINT
    {"", "", "Base", "StrValue", 6, Nb(), Ns(), "YES", "STRING(MAX)"},  // NOLINT
    {"", "", "Base", "ByteValue", 7, Nb(), Ns(), "YES", "BYTES(256)"},  // NOLINT
    {"", "", "Base", "TimestampValue", 8, Nb(), Ns(), "YES", "TIMESTAMP"},  // NOLINT
    {"", "", "Base", "DateValue", 9, Nb(), Ns(), "YES", "DATE"},  // NOLINT
    {"", "", "Base", "BoolArray", 10, Nb(), Ns(), "NO", "ARRAY<BOOL>"},  // NOLINT
    {"", "", "Base", "IntArray", 11, Nb(), Ns(), "YES", "ARRAY<INT64>"},  // NOLINT
    {"", "", "Base", "DoubleArray", 12, Nb(), Ns(), "YES", "ARRAY<FLOAT64>"},  // NOLINT
    {"", "", "Base", "StrArray", 13, Nb(), Ns(), "YES", "ARRAY<STRING(256)>"},  // NOLINT
    {"", "", "Base", "ByteArray", 14, Nb(), Ns(), "YES", "ARRAY<BYTES(MAX)>"},  // NOLINT
    {"", "", "Base", "TimestampArray", 15, Nb(), Ns(), "YES", "ARRAY<TIMESTAMP>"},  // NOLINT
    {"", "", "Base", "DateArray", 16, Nb(), Ns(), "YES", "ARRAY<DATE>"},  // NOLINT
    {"", "", "CascadeChild", "Key1", 1, Nb(), Ns(), "YES", "INT64"},  // NOLINT
    {"", "", "CascadeChild", "Key2", 2, Nb(), Ns(), "YES", "STRING(256)"},  // NOLINT
    {"", "", "CascadeChild", "ChildKey", 3, Nb(), Ns(), "YES", "BOOL"},  // NOLINT
    {"", "", "CascadeChild", "Value1", 4, Nb(), Ns(), "NO", "STRING(MAX)"},  // NOLINT
    {"", "", "CascadeChild", "Value2", 5, Nb(), Ns(), "YES", "BOOL"},  // NOLINT
    {"", "", "NoActionChild", "Key1", 1, Nb(), Ns(), "YES", "INT64"},  // NOLINT
    {"", "", "NoActionChild", "Key2", 2, Nb(), Ns(), "YES", "STRING(256)"},  // NOLINT
    {"", "", "NoActionChild", "ChildKey", 3, Nb(), Ns(), "YES", "BOOL"},  // NOLINT
    {"", "", "NoActionChild", "Value", 4, Nb(), Ns(), "YES", "STRING(MAX)"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultIndexes) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.index_name,
        t.index_type,
        t.parent_table_name,
        t.is_unique,
        t.is_null_filtered,
        t.index_state,
        t.spanner_is_managed
      from
        information_schema.indexes AS t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.index_name
    )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "Base", "IDX_Base_BoolValue_Key2_N_\\w{16}", "INDEX", "", false, true, "READ_WRITE", true},  // NOLINT
    {"", "", "Base", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns(), false},  // NOLINT
    {"", "", "CascadeChild", "CascadeChildByValue", "INDEX", "Base", true, true, "READ_WRITE", false},  // NOLINT
    {"", "", "CascadeChild", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "INDEX", "", true, true, "READ_WRITE", true},  // NOLINT
    {"", "", "CascadeChild", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns(), false},  // NOLINT
    {"", "", "NoActionChild", "NoActionChildByValue", "INDEX", "", false, false, "READ_WRITE", false},  // NOLINT
    {"", "", "NoActionChild", "PRIMARY_KEY", "PRIMARY_KEY", "", true, false, Ns(), false},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultIndexColumns) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.index_name,
        t.column_name,
        t.ordinal_position,
        t.column_ordering,
        t.is_nullable,
        t.spanner_type
      from
        information_schema.index_columns AS t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.index_name,
        t.ordinal_position
    )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "Base", "IDX_Base_BoolValue_Key2_N_\\w{16}", "BoolValue", 1, "ASC", "NO", "BOOL"},  // NOLINT
    {"", "", "Base", "IDX_Base_BoolValue_Key2_N_\\w{16}", "Key2", 2, "DESC", "NO", "STRING(256)"},  // NOLINT
    {"", "", "Base", "PRIMARY_KEY", "Key1", 1, "ASC", "YES", "INT64"},  // NOLINT
    {"", "", "Base", "PRIMARY_KEY", "Key2", 2, "DESC", "YES", "STRING(256)"},  // NOLINT
    {"", "", "CascadeChild", "CascadeChildByValue", "Value1", Ni(), Ns(), "NO", "STRING(MAX)"},  // NOLINT
    {"", "", "CascadeChild", "CascadeChildByValue", "Key1", 1, "ASC", "NO", "INT64"},  // NOLINT
    {"", "", "CascadeChild", "CascadeChildByValue", "Key2", 2, "DESC", "NO", "STRING(256)"},  // NOLINT
    {"", "", "CascadeChild", "CascadeChildByValue", "Value2", 3, "ASC", "NO", "BOOL"},  // NOLINT
    {"", "", "CascadeChild", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "ChildKey", 1, "ASC", "NO", "BOOL"},  // NOLINT
    {"", "", "CascadeChild", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "Value1", 2, "ASC", "NO", "STRING(MAX)"},  // NOLINT
    {"", "", "CascadeChild", "PRIMARY_KEY", "Key1", 1, "ASC", "YES", "INT64"},  // NOLINT
    {"", "", "CascadeChild", "PRIMARY_KEY", "Key2", 2, "DESC", "YES", "STRING(256)"},  // NOLINT
    {"", "", "CascadeChild", "PRIMARY_KEY", "ChildKey", 3, "ASC", "YES", "BOOL"},  // NOLINT
    {"", "", "NoActionChild", "NoActionChildByValue", "Value", 1, "ASC", "YES", "STRING(MAX)"},  // NOLINT
    {"", "", "NoActionChild", "PRIMARY_KEY", "Key1", 1, "ASC", "YES", "INT64"},  // NOLINT
    {"", "", "NoActionChild", "PRIMARY_KEY", "Key2", 2, "DESC", "YES", "STRING(256)"},  // NOLINT
    {"", "", "NoActionChild", "PRIMARY_KEY", "ChildKey", 3, "ASC", "YES", "BOOL"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultColumnOptions) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.option_name,
        t.option_type,
        t.option_value
      from
        information_schema.column_options AS t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.column_name,
        t.option_name
    )");
  LogResults(results);
  // clang-format off
  auto expected = std::vector<ValueRow>({
    {"", "", "Base", "TimestampValue", "allow_commit_timestamp", "BOOL", "TRUE"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultTableConstraints) {
  auto results = Query(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.constraint_type,
        t.is_deferrable,
        t.initially_deferred,
        t.enforced
      from
        information_schema.table_constraints as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = ''
      order by
        t.constraint_name
  )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "CK_IS_NOT_NULL_Base_BoolArray", "", "", "Base", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "", "CK_IS_NOT_NULL_Base_IntValue", "", "", "Base", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "", "CK_IS_NOT_NULL_CascadeChild_Value1", "", "", "CascadeChild", "CHECK", "NO", "NO", "YES"},  // NOLINT
    {"", "", "FKBaseCascadeChild", "", "", "Base", "FOREIGN KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "", "", "CascadeChild", "UNIQUE", "NO", "NO", "YES"},  // NOLINT
    {"", "", "PK_Base", "", "", "Base", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "", "PK_CascadeChild", "", "", "CascadeChild", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
    {"", "", "PK_NoActionChild", "", "", "NoActionChild", "PRIMARY KEY", "NO", "NO", "YES"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultConstraintTableUsage) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name
      from
        information_schema.constraint_table_usage as t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.constraint_name
  )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "Base", "", "", "CK_IS_NOT_NULL_Base_BoolArray"},  // NOLINT
    {"", "", "Base", "", "", "CK_IS_NOT_NULL_Base_IntValue"},  // NOLINT
    {"", "", "Base", "", "", "PK_Base"},  // NOLINT
    {"", "", "CascadeChild", "", "", "CK_IS_NOT_NULL_CascadeChild_Value1"},  // NOLINT
    {"", "", "CascadeChild", "", "", "FKBaseCascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}"},  // NOLINT
    {"", "", "CascadeChild", "", "", "PK_CascadeChild"},  // NOLINT
    {"", "", "NoActionChild", "", "", "PK_NoActionChild"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultReferentialConstraints) {
  auto results = Query(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.unique_constraint_catalog,
        t.unique_constraint_schema,
        t.unique_constraint_name,
        t.match_option,
        t.update_rule,
        t.delete_rule,
        t.spanner_state
      from
        information_schema.referential_constraints as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = ''
      order by
        t.constraint_name
  )");
  LogResults(results);
  auto cascade_index =
      FindString(results, 5, R"(IDX_CascadeChild_ChildKey_Value1_U_\w{16})");
  // clang-format off
  auto expected = std::vector<ValueRow>({
      {"", "", "FKBaseCascadeChild", "", "", cascade_index, "SIMPLE", "NO ACTION", "NO ACTION", "COMMITTED"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultKeyColumnUsage) {
  auto results = Query(R"(
      select
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name,
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.ordinal_position,
        t.position_in_unique_constraint
      from
        information_schema.key_column_usage as t
      where
        t.constraint_catalog = ''
        and t.constraint_schema = ''
      order by
        t.constraint_name,
        t.table_name,
        t.ordinal_position
  )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "FKBaseCascadeChild", "", "", "Base", "BoolValue", 1, 1},  // NOLINT
    {"", "", "FKBaseCascadeChild", "", "", "Base", "Key2", 2, 2},  // NOLINT
    {"", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "", "", "CascadeChild", "ChildKey", 1, Ni()},  // NOLINT
    {"", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}", "", "", "CascadeChild", "Value1", 2, Ni()},  // NOLINT
    {"", "", "PK_Base", "", "", "Base", "Key1", 1, Ni()},  // NOLINT
    {"", "", "PK_Base", "", "", "Base", "Key2", 2, Ni()},  // NOLINT
    {"", "", "PK_CascadeChild", "", "", "CascadeChild", "Key1", 1, Ni()},  // NOLINT
    {"", "", "PK_CascadeChild", "", "", "CascadeChild", "Key2", 2, Ni()},  // NOLINT
    {"", "", "PK_CascadeChild", "", "", "CascadeChild", "ChildKey", 3, Ni()},  // NOLINT
    {"", "", "PK_NoActionChild", "", "", "NoActionChild", "Key1", 1, Ni()},  // NOLINT
    {"", "", "PK_NoActionChild", "", "", "NoActionChild", "Key2", 2, Ni()},  // NOLINT
    {"", "", "PK_NoActionChild", "", "", "NoActionChild", "ChildKey", 3, Ni()},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

TEST_F(InformationSchemaTest, DefaultConstraintColumnUsage) {
  auto results = Query(R"(
      select
        t.table_catalog,
        t.table_schema,
        t.table_name,
        t.column_name,
        t.constraint_catalog,
        t.constraint_schema,
        t.constraint_name
      from
        information_schema.constraint_column_usage as t
      where
        t.table_catalog = ''
        and t.table_schema = ''
      order by
        t.table_name,
        t.column_name,
        t.constraint_name
  )");
  LogResults(results);
  // clang-format off
  auto expected = ExpectedRows(results, {
    {"", "", "Base", "BoolArray", "", "", "CK_IS_NOT_NULL_Base_BoolArray"},  // NOLINT
    {"", "", "Base", "IntValue", "", "", "CK_IS_NOT_NULL_Base_IntValue"},  // NOLINT
    {"", "", "Base", "Key1", "", "", "PK_Base"},  // NOLINT
    {"", "", "Base", "Key2", "", "", "PK_Base"},  // NOLINT
    {"", "", "CascadeChild", "ChildKey", "", "", "FKBaseCascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "ChildKey", "", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}"},  // NOLINT
    {"", "", "CascadeChild", "ChildKey", "", "", "PK_CascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "Key1", "", "", "PK_CascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "Key2", "", "", "PK_CascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "Value1", "", "", "CK_IS_NOT_NULL_CascadeChild_Value1"},  // NOLINT
    {"", "", "CascadeChild", "Value1", "", "", "FKBaseCascadeChild"},  // NOLINT
    {"", "", "CascadeChild", "Value1", "", "", "IDX_CascadeChild_ChildKey_Value1_U_\\w{16}"},  // NOLINT
    {"", "", "NoActionChild", "ChildKey", "", "", "PK_NoActionChild"},  // NOLINT
    {"", "", "NoActionChild", "Key1", "", "", "PK_NoActionChild"},  // NOLINT
    {"", "", "NoActionChild", "Key2", "", "", "PK_NoActionChild"},  // NOLINT
  });
  // clang-format on
  EXPECT_THAT(results, IsOkAndHoldsRows(expected));
}

}  // namespace

}  // namespace test
}  // namespace emulator
}  // namespace spanner
}  // namespace google
