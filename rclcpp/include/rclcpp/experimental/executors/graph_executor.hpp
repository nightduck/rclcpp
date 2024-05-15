// Copyright 2024 Washington University in St Louis
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

#ifndef RCLCPP__EXPERIMENTAL__EXECUTORS__GRAPH_EXECUTOR_HPP_
#define RCLCPP__EXPERIMENTAL__EXECUTORS__GRAPH_EXECUTOR_HPP_

#include <map>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/executor.hpp"
#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"
#include "rclcpp/experimental/executors/events_executor/priority_events_queue.hpp"
#include "rclcpp/experimental/graph_executable.hpp"

namespace rclcpp
{
namespace experimental
{
namespace executors
{
  
class GraphExecutor : public EventsExecutor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(GraphExecutor)

  /// Default constructor. See the default constructor for Executor.
  RCLCPP_PUBLIC
  GraphExecutor(bool execute_timers_separate_thread = false);

  /// Default destructor
  RCLCPP_PUBLIC
  virtual ~GraphExecutor();

  /// Add a node to the executor.
  /**
   * \sa rclcpp::experimental::executors::EventsExecutor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::EventsExecutor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  /// Remove a node from the executor.
  /**
   * \sa rclcpp::experimental::executors::EventsExecutor::remove_node
   */
  RCLCPP_PUBLIC
  void
  remove_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::experimental::executors::EventsExecutor::remove_node
   */
  RCLCPP_PUBLIC
  void
  remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  /// Returns the graph
  RCLCPP_PUBLIC
  const std::multimap<const void *, rclcpp::experimental::graph_node_t::SharedPtr>
  get_graph_nodes();

  /// Assigns priority to graph nodes
  RCLCPP_PUBLIC
  void
  assign_priority();

protected:
  /// Duplicate a graph node and all of its children.
  RCLCPP_PUBLIC
  graph_node_t::SharedPtr
  copy_graph_node_r(const rclcpp::experimental::graph_node_t::SharedPtr & graph_executable);

  /// Recursively add graph_node_t to graph_nodes_
  RCLCPP_PUBLIC
  void
  add_graph_node_r(
    const void * key,
    const rclcpp::experimental::graph_node_t::SharedPtr & graph_node);

  /// Recursively walk down tree in graph and assign everything a priority of zero
  RCLCPP_PUBLIC
  void
  recursively_assign_value(
    const rclcpp::experimental::graph_node_t::SharedPtr & graph_node,
    int64_t priority = 0);

  /// Recursively walk down tree in graph and increment priority as we go deeper
  RCLCPP_PUBLIC
  int
  recursively_increment_priority(
    const rclcpp::experimental::graph_node_t::SharedPtr & graph_node,
    int64_t priority);

  /// Recursively walk down tree in graph and increment priority as we go deeper
  RCLCPP_PUBLIC
  int64_t
  decrement_priority_bfs(
    const rclcpp::experimental::graph_node_t::SharedPtr & graph_node,
    int64_t priority);

  std::multimap<const void *, rclcpp::experimental::graph_node_t::SharedPtr> graph_nodes_;
};

}  // namespace executors
}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__EXECUTORS__GRAPH_EXECUTOR_HPP_
