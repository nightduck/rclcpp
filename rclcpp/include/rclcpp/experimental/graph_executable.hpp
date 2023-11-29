// Copyright 2023 Washington University in St Louis
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RCLCPP__EXPERIMENTAL__EXECUTABLE_GRAPH_HPP_
#define RCLCPP__EXPERIMENTAL__EXECUTABLE_GRAPH_HPP_

#include <string>
#include <vector>
#include <memory>

#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{
namespace experimental
{

typedef struct graph_node_t
{
  typedef std::shared_ptr<graph_node_t> SharedPtr;

  std::string name;
  std::vector<std::string> topics;
  std::vector<graph_node_t::SharedPtr> children;
} graph_node_t;

class GraphExecutable
{
public:
  typedef std::shared_ptr<GraphExecutable> SharedPtr;

  RCLCPP_PUBLIC
  void
  add_graph_child(
    const rclcpp::experimental::GraphExecutable::SharedPtr & child);

  RCLCPP_PUBLIC
  void 
  add_output_topic(
    const std::string & topic_name);
private:
  rclcpp::experimental::graph_node_t::SharedPtr graph_node_;
};

}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__EXECUTABLE_GRAPH_HPP_
