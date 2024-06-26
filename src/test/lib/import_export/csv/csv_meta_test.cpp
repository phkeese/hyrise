#include "nlohmann/json.hpp"

#include "base_test.hpp"
#include "import_export/csv/csv_meta.hpp"

namespace hyrise {

class CsvMetaTest : public BaseTest {};

TEST_F(CsvMetaTest, ProcessCsvMetaFile) {
  auto meta = process_csv_meta_file("resources/test_data/csv/sample_meta_information.csv.json");

  auto meta_expected = CsvMeta{};
  meta_expected.columns.emplace_back(ColumnMeta{"a", "int", false});
  meta_expected.columns.emplace_back(ColumnMeta{"b", "string", false});
  meta_expected.columns.emplace_back(ColumnMeta{"c", "float", true});

  EXPECT_EQ(meta_expected, meta);
}

TEST_F(CsvMetaTest, ProcessCsvMetaFileMissing) {
  EXPECT_THROW(process_csv_meta_file("resources/test_data/csv/missing_file.csv.json"), std::logic_error);
}

TEST_F(CsvMetaTest, JsonSyntaxError) {
  EXPECT_THROW(process_csv_meta_file("resources/test_data/csv/json_syntax_error.csv.json"), nlohmann::json::exception);
}

TEST_F(CsvMetaTest, ParseConfigOnlySingleCharacters) {
  auto json_meta = nlohmann::json::parse(R"(
    {
      "columns": [
        {
          "name": "a",
          "type": "string"
        }
      ],
      "config": {
        "delimiter": "\n\n"
      }
    }
  )");

  auto meta = CsvMeta{};
  EXPECT_THROW(from_json(json_meta, meta), std::logic_error);
}

TEST_F(CsvMetaTest, ColumnsMustBeArray) {
  auto json_meta = nlohmann::json::parse(R"(
    {
      "columns": {}
    }
  )");

  auto meta = CsvMeta{};
  EXPECT_THROW(from_json(json_meta, meta), std::logic_error);
}

}  // namespace hyrise
