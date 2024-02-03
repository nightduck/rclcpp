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

#ifndef RCLCPP__EXPERIMENTAL__GRAPH_EXECUTABLE_HPP_
#define RCLCPP__EXPERIMENTAL__GRAPH_EXECUTABLE_HPP_

#include <string>
#include <vector>
#include <memory>

#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{
namespace experimental
{

typedef struct graph_node graph_node_t;
struct graph_node
{
  typedef std::shared_ptr<graph_node_t> SharedPtr;
  typedef std::unique_ptr<graph_node_t> UniquePtr;

  std::string name;
  std::string input_topic;
  std::vector<std::string> output_topics;
  graph_node_t::SharedPtr parent;
  std::vector<graph_node_t::SharedPtr> children;
  int wcet;
};

class GraphExecutable
{
public:
  typedef std::shared_ptr<GraphExecutable> SharedPtr;
  typedef std::unique_ptr<GraphExecutable> UniquePtr;

  // RCLCPP_PUBLIC
  // void
  // add_graph_child(
  //   const GraphExecutable::SharedPtr & child);

  RCLCPP_PUBLIC
  void
  add_output_topic(
    const std::string & topic_name);

  RCLCPP_PUBLIC
  graph_node_t::SharedPtr
  copy_graph_node();

private:
  graph_node_t::SharedPtr graph_node_;
};

}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__GRAPH_EXECUTABLE_HPP_
