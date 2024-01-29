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

#include "rclcpp/experimental/executors/graph_executor.hpp"
#include "rclcpp/experimental/executors/events_executor/priority_events_queue.hpp"

using rclcpp::experimental::executors::GraphExecutor;
using rclcpp::experimental::executors::EventsExecutor;
using rclcpp::experimental::executors::PriorityEventsQueue;

GraphExecutor::GraphExecutor()
: EventsExecutor(std::make_unique<PriorityEventsQueue>())
{
}

GraphExecutor::GraphExecutor(std::function<int(const ExecutorEvent &)> extract_priority)
: EventsExecutor(std::make_unique<PriorityEventsQueue>(extract_priority))
{
}

GraphExecutor::~GraphExecutor()
{
}

void
GraphExecutor::add_node(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  EventsExecutor::add_node(node_ptr, notify);

  // Examine timers and subs in node and add to graph
  node_ptr->for_each_callback_group(
    [this](const rclcpp::CallbackGroup::WeakPtr & weak_group_ptr) {
      auto group_ptr = weak_group_ptr.lock();
      if (!group_ptr) {
        return;
      }
      if (group_ptr->can_be_taken_from().load()) {
        group_ptr->collect_all_ptrs(
          [this, weak_group_ptr](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
            // Iterate over graph_nodes_
            for (const auto& node : graph_nodes_) {
              // Access the key (executable entity) and value (graph node)
              const void* entity = node.first;
              const graph_node_t::SharedPtr& node_ptr = node.second;

              // Add subscription to graph
              graph_nodes_[subscription->get_subscription_handle()] = subscription->graph_node_;

              // If node_ptr's output topics contain the subscription's topic name, then add the
              // subscription to the graph node as a child. Subscription can be added multiple times
              for (const auto& topic : node_ptr->output_topics) {
                if (topic == subscription->get_topic_name()) {
                  node_ptr->children.emplace_back(subscription);
                  subscription->graph_node_->parent = node_ptr;
                }
              }

              // TODO
              // If node_ptr is subscription and input topic matches one of subscription's output
              // topics, then add the subscription to the graph node as a parent.
            }
          },
          [this, weak_group_ptr](const rclcpp::ServiceBase::SharedPtr & timer) {
            // Iterate over graph_nodes_
            for (const auto& node : graph_nodes_) {
              // Access the key (executable entity) and value (graph node)
              const void* entity = node.first;
              const graph_node_t::SharedPtr& node_ptr = node.second;

              // TODO
              // If node_ptr is subscription and input topic matches one of subscription's output
              // topics, then add the subscription to the graph node as a parent
            }
          },
          [this, weak_group_ptr](const rclcpp::ClientBase::SharedPtr & timer) {
            // TODO: Add client to graph
          },
          [this, weak_group_ptr](const rclcpp::TimerBase::SharedPtr & timer) {
            // TODO: Add timer to graph
          },
          [this, weak_group_ptr](const rclcpp::Waitable::SharedPtr & waitable) {
            // TODO: Add waitable to graph
          });
      }
    });

  // TODO: Calculate ordering of graph and assign priorities or deadlines
}

void
GraphExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->add_node(node_ptr->get_node_base_interface(), notify);
}

void
GraphExecutor::remove_node(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  // TODO: Assert executor is not running

  // TODO: Find timers and subs in node and remove from graph
  // TODO: Recalculate ordering of graph and assign priorities or deadlines

  EventsExecutor::remove_node(node_ptr, notify);

}

void
GraphExecutor::remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->remove_node(node_ptr->get_node_base_interface(), notify);
}