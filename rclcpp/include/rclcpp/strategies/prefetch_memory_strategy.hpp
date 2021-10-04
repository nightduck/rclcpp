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

#include <forward_list>
#include <memory>
#include <queue>
#include <stack>
#include <type_traits>
#include <vector>

#include "rcl/allocator.h"

#include "rclcpp/allocator/allocator_common.hpp"
#include "rclcpp/strategies/allocator_memory_strategy.hpp"
#include "rclcpp/memory_strategy.hpp"
#include "rclcpp/exceptions.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/visibility_control.hpp"

#include "rcutils/logging_macros.h"

#include "rmw/types.h"

namespace rclcpp
{
namespace memory_strategies
{
namespace prefetch_memory_strategy
{

/// Delegate for handling memory allocations while the Executor is executing.
/**
 * By default, the memory strategy dynamically allocates memory for structures that come in from
 * the rmw implementation after the executor waits for work, based on the number of entities that
 * come through.
 */
template<
  typename Alloc = std::allocator<void>,
  typename Adaptor = std::priority_queue<AnyExecutable>
  >
class PrefetchMemoryStrategy
    : public memory_strategies::allocator_memory_strategy::AllocatorMemoryStrategy<Alloc>
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(PrefetchMemoryStrategy)

  // Type checking
  static_assert(
    std::is_base_of<std::priority_queue<AnyExecutable>, Adaptor>::value ||
    std::is_base_of<std::queue<AnyExecutable>, Adaptor>::value ||
    std::is_base_of<std::stack<AnyExecutable>, Adaptor>::value,
    "Adaptor must be a descendent of a queue, stack, or priority queue, and must use AnyExecutable \
    Container, and Compare as its arguments"
  );
  static_assert(std::is_same<AnyExecutable, typename Adaptor::value_type>::value,
    "value_type of adaptor must be AnyExecutable");

  using VoidAllocTraits = typename allocator::AllocRebind<void *, Alloc>;
  using VoidAlloc = typename VoidAllocTraits::allocator_type;

  explicit PrefetchMemoryStrategy(std::shared_ptr<Alloc> allocator)
  {
    allocator_ = std::make_shared<VoidAlloc>(*allocator.get());
  }

  PrefetchMemoryStrategy()
  {
    allocator_ = std::make_shared<VoidAlloc>();
  }

  bool
  get_subscription_message(
    std::shared_ptr<void> message,
    rclcpp::SubscriptionBase::SharedPtr subscription) {
    rclcpp::MessageInfo message_info;
    message_info.get_rmw_message_info().from_intra_process = false;
  
    if (subscription->is_serialized()) {
      auto serial_message = subscription->create_serialized_message();
      try {
        bool taken = false;
        taken = subscription->take_serialized(*serial_message.get(), message_info);
        message = std::static_pointer_cast<void>(serial_message);
        return taken;
      } catch (const rclcpp::exceptions::RCLError & rcl_error) {
        RCLCPP_ERROR(
          rclcpp::get_logger("rclcpp"),
          "executor %s '%s' unexpectedly failed: %s",
          "taking a serialized message from topic",
          subscription->get_topic_name(),
          rcl_error.what());
        return false;
      }
    } else if (subscription->can_loan_messages()) {
      void * loaned_msg = nullptr;
      try {
        rcl_ret_t ret = rcl_take_loaned_message(
            subscription->get_subscription_handle().get(),
            &loaned_msg,
            &message_info.get_rmw_message_info(),
            nullptr);
        if (RCL_RET_SUBSCRIPTION_TAKE_FAILED == ret) {
            return false;
        } else if (RCL_RET_OK != ret) {
          rclcpp::exceptions::throw_from_rcl_error(ret);
        }
        message.reset(loaned_msg);    // TODO: This might be bad. After returning, the shared_ptr is
                                      //       destroyed, deleting the loaned message. Maybe give it
                                      //       a pointer to a pointer, like above
        return true;
      } catch (const rclcpp::exceptions::RCLError & rcl_error) {
        RCLCPP_ERROR(
          rclcpp::get_logger("rclcpp"),
          "executor %s '%s' unexpectedly failed: %s",
          "taking a loaned message from topic",
          subscription->get_topic_name(),
          rcl_error.what());
        return false;
      }
    } else {
      message = subscription->create_message();
      try {
        return subscription->take_type_erased(message.get(), message_info);
      } catch (const rclcpp::exceptions::RCLError & rcl_error) {
        RCLCPP_ERROR(
          rclcpp::get_logger("rclcpp"),
          "executor %s '%s' unexpectedly failed: %s",
          "taking a message from topic",
          subscription->get_topic_name(),
          rcl_error.what());
        return false;
      }
    }
  }

  bool
  get_next_executable(
    rclcpp::AnyExecutable & any_exec
  ) {
    // TODO: Find a more efficient way to skip over excluded work. Insertions and deletions are
    //       each O(logn) from the priority queue
    // TODO: As is, it does not consider weak_groups_to_nodes, 

    if (released_work_.empty()) {
      return false;
    }

    any_exec = released_work_.top();
    released_work_.pop();

    // If I can't run this work, keep searching until I find one I can
    std::forward_list<AnyExecutable> excluded_work;
    while (!can_run(any_exec) && !released_work_.empty())
    {
      excluded_work.push_front(any_exec);
      any_exec = released_work_.top();
      released_work_.pop();
    }
    
    // Put them all back
    for(AnyExecutable ae : excluded_work) {
      released_work_.push(ae);
    }
  }

  void
  collect_work(
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes
    )
  {
    // Get all subscriptions and their messages
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
        // Otherwise it is safe to set and return the any_exec
        // Fetch all messages awaiting this subscription
        std::shared_ptr<void> msg;

        while(get_subscription_message(msg, subscription)) { // && msg != nullptr ???
          released_work_.emplace({subscription, nullptr, nullptr, nullptr, nullptr, group,
                                  get_node_by_group(group, weak_groups_to_nodes), msg});
        }
        subscription_handles_.erase(it);
        return;
      }
      // Else, the subscription is no longer valid, remove it and continue
      it = subscription_handles_.erase(it);
    }

    // Get all triggered timers
    it = timer_handles_.begin();
    while (it != timer_handles_.end()) {
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
        if (!timer->call()) {
          // timer was cancelled, skip it.
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        released_work_.emplace({nullptr, timer, nullptr, nullptr, nullptr, group,
                                get_node_by_group(group, weak_groups_to_nodes), nullptr});
        timer_handles_.erase(it);
      }
      // Else, the timer is no longer valid, remove it and continue
      it = timer_handles_.erase(it);
    }

    // Get all services and their requests
    it = service_handles_.begin();
    while (it != service_handles_.end()) {
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
        // Otherwise it is safe to set and return the any_exec
        // Fetch all messages awaiting this service
        auto request_header = service->create_request_header();
        std::shared_ptr<void> request = service->create_request();
        
        while(service->take_type_erased_request(request.get(), *request_header)) {
          released_work_.emplace({nullptr, nullptr, service, nullptr, nullptr, group,
                                  get_node_by_group(group, weak_groups_to_nodes), request});
        }
        service_handles_.erase(it);
      }
      // Else, the service is no longer valid, remove it and continue
      it = service_handles_.erase(it);
    }

    // Get all clients and their responses
    it = client_handles_.begin();
    while (it != client_handles_.end()) {
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
        // Otherwise it is safe to set and return the any_exec
        // Fetch all messages awaiting this client
        auto request_header = client->create_request_header();
        std::shared_ptr<void> response = client->create_response();
        
        while(client->take_type_erased_response(response.get(), *request_header)) {
          released_work_.emplace({nullptr, nullptr, nullptr, client, nullptr, group,
                                  get_node_by_group(group, weak_groups_to_nodes), response});
        }
        client_handles_.erase(it);
      }
      // Else, the service is no longer valid, remove it and continue
      it = client_handles_.erase(it);
    }

    // Get all waitables
    it = waitable_handles_.begin();
    while (it != waitable_handles_.end()) {
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
        // Otherwise it is safe to set and return the any_exec
        released_work_.emplace({nullptr, nullptr, nullptr, nullptr, waitable, group,
                                get_node_by_group(group, weak_groups_to_nodes), nullptr});
        waitable_handles_.erase(it);
        return;
      }
      // Else, the waitable is no longer valid, remove it and continue
      it = waitable_handles_.erase(it);
    }
  }

  bool can_run(const AnyExecutable& any_exec) {
    return any_exec.callback_group->can_be_taken_from().load();
  }

private:
  template<typename T>
  using VectorRebind =
    std::vector<T, typename std::allocator_traits<Alloc>::template rebind_alloc<T>>;

  VectorRebind<const rcl_guard_condition_t *> guard_conditions_;

  VectorRebind<std::shared_ptr<const rcl_subscription_t>> subscription_handles_;
  VectorRebind<std::shared_ptr<const rcl_service_t>> service_handles_;
  VectorRebind<std::shared_ptr<const rcl_client_t>> client_handles_;
  VectorRebind<std::shared_ptr<const rcl_timer_t>> timer_handles_;
  VectorRebind<std::shared_ptr<Waitable>> waitable_handles_;

  Adaptor released_work_;

  std::shared_ptr<VoidAlloc> allocator_;

  template<typename T>
  void do_nothing(T t) const { return; }
};

}  // namespace prefetch_memory_strategy
}  // namespace memory_strategies
}  // namespace rclcpp

#endif  // RCLCPP__STRATEGIES__ALLOCATOR_MEMORY_STRATEGY_HPP_
