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

#include "rclcpp/experimental/graph_executable.hpp"

namespace rclcpp
{
namespace experimental
{

void
GraphExecutable::add_graph_child(
  const rclcpp::experimental::GraphExecutable::SharedPtr & child)
{
  graph_node_->children.emplace_back(child->graph_node_);
}

void
GraphExecutable::add_output_topic(
  const std::string & topic_name)
{
  graph_node_->output_topics.emplace_back(topic_name);
}

}  // namespace experimental
}  // namespace rclcpp
