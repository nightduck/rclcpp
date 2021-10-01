// Copyright 2015 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__STRATEGIES__PREFETCH_MEMORY_STRATEGY_HPP_
#define RCLCPP__STRATEGIES__PREFETCH_MEMORY_STRATEGY_HPP_

#include <memory>
#include <queue>
#include <stack>
#include <vector>

#include "rcl/allocator.h"

#include "rclcpp/allocator/allocator_common.hpp"
#include "rclcpp/memory_strategy.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/visibility_control.hpp"

#include "rcutils/logging_macros.h"

#include "rmw/types.h"

namespace rclcpp
{
namespace memory_strategies
{
namespace allocator_memory_strategy
{

/// Delegate for handling memory allocations while the Executor is executing
/**
 * By default, the memory strategy dynamically allocates memory for structures that come in from
 * the rmw implementation after the executor waits for work, based on the number of entities that
 * come through. This implementation also pre-fetches messages from subscriptions and services. The
 * memory_strategy_ variable must be overriden as this type to use this class's functionality
 */
template<
  typename Alloc = std::allocator<void>
  >
class PrefetchMemoryStrategy
  : public memory_strategies::allocator_memory_strategy::AllocatorMemoryStrategy<Alloc>
{
  explicit ReadQueueMemoryStrategy(std::shared_ptr<Alloc> allocator)
    : allocator_memory_strategy::AllocatorMemoryStrategy<Alloc>(allocator) {}

  ReadQueueMemoryStrategy()
    : allocator_memory_strategy::AllocatorMemoryStrategy<Alloc>() {}

  std::vector<rclcpp::AnyExecutable>::iterator
  get_ready_subscriptions(
    std::vector<rclcpp::AnyExecutable>::iterator ae_it,
    std::vector<rclcpp::AnyExecutable>::iterator ae_fin,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes)
  {
    auto it = subscription_handles_.begin();
    while (it != subscription_handles_.end() && ae_it < ae_fin) {
      auto subscription = get_subscription_by_handle(*it, weak_groups_to_nodes);
      if (subscription) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_subscription(subscription, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the subscription is not valid...
          // Remove it from the ready list and continue looking
          it = subscription_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        (*ae_it).subscription = subscription;
        (*ae_it).callback_group = group;
        (*ae_it).node_base = get_node_by_group(group, weak_groups_to_nodes);
        subscription_handles_.erase(it);
        ae_it++;
      } else {
        // Else, the subscription is no longer valid, remove it and continue
        it = subscription_handles_.erase(it);
      }
    }
    return ae_it;
  }

  std::vector<rclcpp::AnyExecutable>::iterator
  get_ready_services(
    std::vector<rclcpp::AnyExecutable>::iterator ae_it,
    std::vector<rclcpp::AnyExecutable>::iterator ae_fin,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) 
  {
    auto it = service_handles_.begin();
    while (it != service_handles_.end() && ae_it < ae_fin) {
      auto service = get_service_by_handle(*it, weak_groups_to_nodes);
      if (service) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_service(service, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the service is not valid...
          // Remove it from the ready list and continue looking
          it = service_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        (*ae_it).service = service;
        (*ae_it).callback_group = group;
        (*ae_it).node_base = get_node_by_group(group, weak_groups_to_nodes);
        service_handles_.erase(it);
        ae_it++;
      } else {
        // Else, the service is no longer valid, remove it and continue
        it = service_handles_.erase(it);
      }
    }
    return ae_it;
  }

  std::vector<rclcpp::AnyExecutable>::iterator
  get_ready_clients(
    std::vector<rclcpp::AnyExecutable>::iterator ae_it,
    std::vector<rclcpp::AnyExecutable>::iterator ae_fin,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes)
  {
    auto it = client_handles_.begin();
    while (it != subscription_handles_.end() && ae_it < ae_fin) {
      auto client = get_client_by_handle(*it, weak_groups_to_nodes);
      if (client) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_client(client, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the service is not valid...
          // Remove it from the ready list and continue looking
          it = client_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        (*ae_it).client = client;
        (*ae_it).callback_group = group;
        (*ae_it).node_base = get_node_by_group(group, weak_groups_to_nodes);
        client_handles_.erase(it);
        ae_it++;
      } else {
        // Else, the service is no longer valid, remove it and continue
        it = client_handles_.erase(it);
      }
    }
    return ae_it;
  }

  std::vector<rclcpp::AnyExecutable>::iterator
  get_ready_timers(
    std::vector<rclcpp::AnyExecutable>::iterator ae_it,
    std::vector<rclcpp::AnyExecutable>::iterator ae_fin,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes)
  {
    auto it = timer_handles_.begin();
    while (it != subscription_handles_.end() && ae_it < ae_fin) {
      auto timer = get_timer_by_handle(*it, weak_groups_to_nodes);
      if (timer) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_timer(timer, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the timer is not valid...
          // Remove it from the ready list and continue looking
          it = timer_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        if (!timer->call()) {
          // timer was cancelled, skip it.
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        (*ae_it).timer = timer;
        (*ae_it).callback_group = group;
        (*ae_it).node_base = get_node_by_group(group, weak_groups_to_nodes);
        timer_handles_.erase(it);
        ae_it++;
      } else {
        // Else, the timer is no longer valid, remove it and continue
        it = timer_handles_.erase(it);
      }
    }
    return ae_it;
  }

  std::vector<rclcpp::AnyExecutable>::iterator
  get_ready_waitables(
    std::vector<rclcpp::AnyExecutable>::iterator ae_it,
    std::vector<rclcpp::AnyExecutable>::iterator ae_fin,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes)
  {
    auto it = waitable_handles_.begin();
    while (it != subscription_handles_.end() && ae_it < ae_fin) {
      auto waitable = *it;
      if (waitable) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_waitable(waitable, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the waitable is not valid...
          // Remove it from the ready list and continue looking
          it = waitable_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        (*ae_it).waitable = waitable;
        (*ae_it).callback_group = group;
        (*ae_it).node_base = get_node_by_group(group, weak_groups_to_nodes);
        waitable_handles_.erase(it);
        ae_it++;
      } else {
        // Else, the waitable is no longer valid, remove it and continue
        it = waitable_handles_.erase(it);
      }
    }
    return ae_it;
  }

  
  std::shared_ptr<void>
  get_subscription_message(rclcpp::SubscriptionBase::SharedPtr subscription) {
    rclcpp::MessageInfo message_info;
    message_info.get_rmw_message_info().from_intra_process = false;

    if (subscription->is_serialized()) {
      std::shared_ptr<SerializedMessage> serialized_msg = subscription->create_serialized_message();
      subscription->take_serialized(*serialized_msg.get(), message_info);
      return serialized_msg;
    } else if (subscription->can_loan_messages()) {
      void * loaned_msg = nullptr;
      rcl_ret_t ret = rcl_take_loaned_message(
          subscription->get_subscription_handle().get(),
          &loaned_msg,
          &message_info.get_rmw_message_info(),
          nullptr);
      return loaned_msg;
    } else {
      std::shared_ptr<void> message = subscription->create_message();
      subscription->take_type_erased(message.get(), message_info);
      return message;
    }
  }

  template<
    class Container = std::deque<AnyExecutable, Alloc>
  >
  void
  collect_work(std::queue<AnyExecutable, Container>)
  {

  }

  template<
    class Container = std::deque<AnyExecutable, Alloc>
  >
  void
  collect_work(std::stack<AnyExecutable, Container>)
  {

  }

  template<
    class Container = std::vector<AnyExecutable, Alloc>,
    class Compare = std::less<AnyExecutable>
  >
  void
  collect_work(std::priority_queue<AnyExecutable, Container, Compare>& queue)
  {
    auto it = subscription_handles_.begin();
    while (it != subscription_handles_.end()) {
      auto subscription = get_subscription_by_handle(*it, weak_groups_to_nodes);
      if (subscription) {
        // Find the group for this handle and see if it can be serviced
        auto group = get_group_by_subscription(subscription, weak_groups_to_nodes);
        if (!group) {
          // Group was not found, meaning the subscription is not valid...
          // Remove it from the ready list and continue looking
          it = subscription_handles_.erase(it);
          continue;
        }
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        // Fetch all messages awaiting this subscription
        std::shared_ptr<void> msg = get_message(subscription);
        while(msg != null) {
          queue.emplace({subscription, null, null, null, null, group, get_node_by_group(group, weak_groups_to_nodes), msg});
        }
        subscription_handles_.erase(it);
        return;
      }
      // Else, the subscription is no longer valid, remove it and continue
      it = subscription_handles_.erase(it);
    }
  }

};
}  // namespace allocator_memory_strategy
}  // namespace memory_strategies
}  // namespace rclcpp

#endif  // RCLCPP__STRATEGIES__PREFETCH_MEMORY_STRATEGY_HPP_