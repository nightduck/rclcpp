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

using rclcpp::experimental::executors::GraphExecutor;
using rclcpp::experimental::executors::EventsExecutorRT;
using rclcpp::experimental::executors::PriorityEventsQueue;

GraphExecutor::GraphExecutor(EventsQueue::UniquePtr timers_queue)
: EventsExecutorRT(std::make_unique<PriorityEventsQueue>(), std::move(timers_queue))
{
}

GraphExecutor::~GraphExecutor()
{
}

void
GraphExecutor::add_node(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  EventsExecutorRT::add_node(node_ptr, notify);

  // Examine timers and subs in node and add to graph
  node_ptr->for_each_callback_group(
    [this](const rclcpp::CallbackGroup::WeakPtr & weak_group_ptr) {
      auto group_ptr = weak_group_ptr.lock();
      if (!group_ptr) {
        return;
      }
      // TODO(nightduck): Reassess variable names, esp node_ptr, and node_ptr_copy
      if (group_ptr->can_be_taken_from().load()) {
        group_ptr->collect_all_ptrs(
          [this, weak_group_ptr](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
            std::list<std::pair<const void *, graph_node_t::SharedPtr>> children_to_insert;
            std::list<std::pair<const void *, graph_node_t::SharedPtr>> parents;

            // Get graph_node object for the subscription, and insert one copy into graph
            auto sub_node = subscription->copy_graph_node();
            sub_node->key = reinterpret_cast<void *>(subscription->get_subscription_handle().get());

            // Iterate over graph_nodes
            for (const auto & relative_node : graph_nodes_) {
              // Access the key (executable entity) and value (graph node)
              const void * entity = relative_node.first;
              graph_node_t::SharedPtr relative = relative_node.second;

              // If relative (or a copy of it) is already a child of the subscription, then skip
              if (sub_node->children.find(entity) != sub_node->children.end()) {
                continue;
              }

              // If relative is subscription and input topic matches one of subscription's output
              // topics, then link the subscription to this relative as a parent (relative is child)
              if (std::find(
                sub_node->output_topics.begin(), sub_node->output_topics.end(),
                relative->input_topic) != sub_node->output_topics.end())
              {
                // Add a relative_copy object for every instance of the subscription in the graph
                if (relative->parent != nullptr) {  // Create copy if parent already exists
                  relative = copy_graph_node_r(relative);
                  children_to_insert.push_back(std::make_pair(entity, relative));
                }

                // Link the two nodes
                sub_node->children[entity] = relative;
                relative->parent = sub_node;
              }

              // If relative's output topics contain the subscription's topic name, then mark the
              // node to be linked as a parent later
              for (const auto & topic : relative->output_topics) {
                if (topic == subscription->get_topic_name()) {
                  parents.push_back(std::make_pair(entity, relative));
                }
              }
            }

            // Insert the subscription into the graph
            graph_nodes_.insert(std::make_pair(sub_node->key, sub_node));

            // Add all contents of children_to_insert to graph_nodes_
            for (const auto & node : children_to_insert) {
              add_graph_node_r(node.first, node.second);
            }

            // Pop one parent from parents and link it to the subscription
            if (!parents.empty()) {
              auto parent = parents.front();
              parents.pop_front();
              auto parent_node = parent.second;
              parent_node->children[sub_node->key] = sub_node;
              sub_node->parent = parent_node;
              sub_node->period = parent_node->period;
            }

            // Iterate over rest of parents and copy the subscription for each
            for (const auto & parent : parents) {
              auto parent_node = parent.second;
              sub_node = copy_graph_node_r(sub_node);
              parent_node->children[sub_node->key] = sub_node;
              sub_node->parent = parent_node;
              sub_node->period = parent_node->period;
              add_graph_node_r(sub_node->key, sub_node);
            }

          },
          [this, weak_group_ptr](const rclcpp::ServiceBase::SharedPtr & service) {
            // Iterate over graph_nodes_
            // for (const auto & node : graph_nodes_) {
            //   // Access the key (executable entity) and value (graph node)
            //   const void * entity = node.first;
            //   const graph_node_t::SharedPtr& node_ptr = node.second;

            //   // TODO
            //   // If node_ptr is subscription and input topic matches one of subscription's output
            //   // topics, then add the subscription to the graph node as a parent
            // }
          },
          [this, weak_group_ptr](const rclcpp::ClientBase::SharedPtr & client) {
            // TODO(nightduck): Add client to graph
          },
          [this, weak_group_ptr](const rclcpp::TimerBase::SharedPtr & timer) {
            std::list<std::pair<const void *, graph_node_t::SharedPtr>> children_to_insert;

            // Get graph_node object for the subscription
            auto tmr_node = timer->copy_graph_node();
            tmr_node->key = reinterpret_cast<void *>(timer.get());
            rcl_timer_get_period(timer->get_timer_handle().get(), &tmr_node->period);

            // Iterate over graph_nodes_
            for (const auto & child_node : graph_nodes_) {
              // Access the key (executable entity) and value (graph node)
              const void * entity = child_node.first;
              graph_node_t::SharedPtr child = child_node.second;

              // If relative (or a copy of it) is already a child of the subscription, then skip
              if (tmr_node->children.find(entity) != tmr_node->children.end()) {
                continue;
              }

              // If child is subscription and input topic matches one of timer's output
              // topics, then link the timer to this child as a parent
              if (std::find(
                tmr_node->output_topics.begin(), tmr_node->output_topics.end(),
                child->input_topic) != tmr_node->output_topics.end())
              {
                // Create copy if parent already exists
                if (child->parent != nullptr) {
                  child = copy_graph_node_r(child);
                  children_to_insert.push_back(std::make_pair(entity, child));
                }

                // Link the two nodes
                tmr_node->children[entity] = child;
                child->parent = tmr_node;
                child->period = tmr_node->period;
              }
            }
            // Insert the timer into the graph, without recursively adding children
            graph_nodes_.insert(std::make_pair(tmr_node->key, tmr_node));

            // Add any duplicated children
            for (const auto & node : children_to_insert) {
              add_graph_node_r(node.first, node.second);
            }
          },
          [this, weak_group_ptr](const rclcpp::Waitable::SharedPtr & waitable) {
            // TODO(nightduck): Add waitable to graph
          });
      }
    });

  // TODO(nightduck): Remove?
  // assign_priority();
}

void
GraphExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->add_node(node_ptr->get_node_base_interface(), notify);
}

void
GraphExecutor::remove_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  // TODO(nightduck): Assert executor is not running

  // TODO(nightduck): Find timers and subs in node and remove from graph
  // TODO(nightduck): Recalculate ordering of graph and assign priorities or deadlines

  EventsExecutorRT::remove_node(node_ptr, notify);
}

void
GraphExecutor::remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->remove_node(node_ptr->get_node_base_interface(), notify);
}

rclcpp::experimental::graph_node_t::SharedPtr
GraphExecutor::copy_graph_node_r(
  const rclcpp::experimental::graph_node_t::SharedPtr & graph_executable)
{
  auto copy = std::make_shared<graph_node_t>(*graph_executable);

  for (auto & child : copy->children) {
    child.second = copy_graph_node_r(child.second);
    child.second->parent = copy;
    child.second->period = copy->period;
  }

  return copy;
}

void
GraphExecutor::add_graph_node_r(
  const void * key,
  const rclcpp::experimental::graph_node_t::SharedPtr & graph_node)
{
  graph_nodes_.insert(std::make_pair(key, graph_node));
  for (auto & child : graph_node->children) {
    add_graph_node_r(child.first, child.second);
  }
}

const std::multimap<const void *, rclcpp::experimental::graph_node_t::SharedPtr>
GraphExecutor::get_graph_nodes()
{
  return graph_nodes_;
}

// void
// GraphExecutor::assign_priority()
// {
//   int priority = 0;
//   for (const auto & node : graph_nodes_) {
//     // Access the key (executable entity) and value (graph node)
//     // const void * entity = node.first;
//     graph_node_t::SharedPtr node_ptr = node.second;
//     if (node_ptr->parent == nullptr) {
//       node_ptr->priority = priority;
//       priority++;
//       priority = recursively_increment_priority(node_ptr, priority);
//     }
//   }
// }

void GraphExecutor::assign_priority()
{
  // Extract all members of graph_nodes_ that don't have parents
  std::vector<graph_node_t::SharedPtr> nodesWithoutParents;
  for (const auto & node : graph_nodes_) {
    if (node.second->parent == nullptr) {
      nodesWithoutParents.push_back(node.second);
    }
  }

  // Sort the timers by shortest to longest period, orphaned subscriptions are first
  std::sort(
    nodesWithoutParents.begin(), nodesWithoutParents.end(),
    [](const graph_node_t::SharedPtr & a, const graph_node_t::SharedPtr & b) {
      // if (a->input_topic != "") {
      //   return true;
      // } else if (b->input_topic != "") {
      //   return false;
      // } else {
      //   return a->period < b->period;
      // }
      return a->period > b->period;
    });

  // Call recursively_increment_priority on each element of the list
  int priority = graph_nodes_.size();
  for (const auto & node : nodesWithoutParents) {
    priority = decrement_priority_bfs(node, priority);
  }
}

void
GraphExecutor::recursively_assign_value(
  const graph_node_t::SharedPtr & graph_node,
  int64_t priority)
{
  graph_node->priority = priority;
  static_cast<PriorityEventsQueue *>(events_queue_.get())->set_priority(graph_node->key, priority);
  for (auto & child : graph_node->children) {
    recursively_assign_value(child.second, priority);
  }
  return;
}

// TODO: This is DFS, correct it to be BFS
int
GraphExecutor::recursively_increment_priority(
  const graph_node_t::SharedPtr & graph_node,
  int64_t priority)
{
  graph_node->priority = priority;
  static_cast<PriorityEventsQueue *>(events_queue_.get())->set_priority(graph_node->key, priority);
  priority++;
  for (auto & child : graph_node->children) {
    priority = recursively_increment_priority(child.second, priority);
  }
  return priority;
}

int64_t
GraphExecutor::decrement_priority_bfs(
  const graph_node_t::SharedPtr & graph_node,
  int64_t priority)
{
  std::queue<graph_node_t::SharedPtr> queue;
  queue.push(graph_node);

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop();
    for (auto & child : node->children) {
      queue.push(child.second);
    }

    node->priority = priority;
    static_cast<PriorityEventsQueue *>(events_queue_.get())->set_priority(node->key, priority);
    priority--;
  }

  return priority;
}
