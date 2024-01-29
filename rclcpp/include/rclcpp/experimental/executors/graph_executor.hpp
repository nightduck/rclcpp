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

#ifndef RCLCPP__EXPERIMENTAL__GRAPH_EXECUTOR_HPP_
#define RCLCPP__EXPERIMENTAL__GRAPH_EXECUTOR_HPP_

#include <map>

#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"

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
  GraphExecutor();
  
  /// Constructor to specify priority callback function.
  RCLCPP_PUBLIC
  explicit GraphExecutor(std::function<int(const ExecutorEvent &)> extract_priority);

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

private:
  std::multimap<void *, graph_node_t::SharedPtr> graph_nodes_;
};

}  // namespace executors
}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__GRAPH_EXECUTOR_HPP_