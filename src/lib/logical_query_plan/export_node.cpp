#include "export_node.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/container_hash/hash.hpp>

#include "magic_enum.hpp"

#include "import_export/file_type.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/abstract_non_query_node.hpp"

namespace hyrise {

ExportNode::ExportNode(const std::string& init_file_name, const FileType init_file_type)
    : AbstractNonQueryNode(LQPNodeType::Export), file_name(init_file_name), file_type(init_file_type) {}

std::string ExportNode::description(const DescriptionMode /*mode*/) const {
  auto file_type_str = std::string{magic_enum::enum_name(file_type)};
  boost::algorithm::to_lower(file_type_str);
  return "[Export] to '" + file_name + "' (" + file_type_str + ")";
}

size_t ExportNode::_on_shallow_hash() const {
  auto hash = size_t{0};
  boost::hash_combine(hash, file_name);
  boost::hash_combine(hash, file_type);
  return hash;
}

std::shared_ptr<AbstractLQPNode> ExportNode::_on_shallow_copy(LQPNodeMapping& /*node_mapping*/) const {
  return ExportNode::make(file_name, file_type);
}

bool ExportNode::_on_shallow_equals(const AbstractLQPNode& rhs, const LQPNodeMapping& /*node_mapping*/) const {
  const auto& export_node = static_cast<const ExportNode&>(rhs);
  return file_name == export_node.file_name && file_type == export_node.file_type;
}

}  // namespace hyrise
