#pragma once

#include <optional>
#include <string>

namespace hyrise {

struct SQLIdentifier final {
  SQLIdentifier(const std::string& init_column_name, const std::optional<std::string>& init_table_name = std::nullopt);

  bool operator==(const SQLIdentifier& rhs) const;

  std::string as_string() const;

  std::string column_name;
  std::optional<std::string> table_name = std::nullopt;
};

}  // namespace hyrise
