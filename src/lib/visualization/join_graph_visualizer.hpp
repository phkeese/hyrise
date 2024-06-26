#pragma once

#include <memory>
#include <string>
#include <vector>

#include "optimizer/join_ordering/join_graph.hpp"
#include "visualization/abstract_visualizer.hpp"

namespace hyrise {

class JoinGraphVisualizer : public AbstractVisualizer<std::vector<JoinGraph>> {
 public:
  using AbstractVisualizer<std::vector<JoinGraph>>::AbstractVisualizer;

 protected:
  void _build_graph(const std::vector<JoinGraph>& graphs) override;
  static std::string _create_vertex_description(const std::shared_ptr<AbstractLQPNode>& vertex);
};

}  // namespace hyrise
