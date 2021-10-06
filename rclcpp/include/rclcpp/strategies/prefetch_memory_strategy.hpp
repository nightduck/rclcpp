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
  typename Adaptor = std::priority_queue<std::shared_ptr<AnyExecutable>>
  >
class PrefetchMemoryStrategy
    : public memory_strategy::MemoryStrategy
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(PrefetchMemoryStrategy)

  // Type checking
  static_assert(
    std::is_base_of<std::priority_queue<std::shared_ptr<AnyExecutable>>, Adaptor>::value ||
    std::is_base_of<std::queue<std::shared_ptr<AnyExecutable>>, Adaptor>::value ||
    std::is_base_of<std::stack<std::shared_ptr<AnyExecutable>>, Adaptor>::value,
    "Adaptor must be a descendent of a queue, stack, or priority queue, and must use AnyExecutable \
    Container, and Compare as its arguments"
  );
  static_assert(std::is_same<std::shared_ptr<AnyExecutable>, typename Adaptor::value_type>::value,
    "value_type of adaptor must be AnyExecutable");

  using VoidAllocTraits = typename allocator::AllocRebind<void *, Alloc>;
  using VoidAlloc = typename VoidAllocTraits::allocator_type;

  explicit PrefetchMemoryStrategy(std::shared_ptr<Alloc> allocator, size_t num_workers = 0)
  {
    allocator_ = std::make_shared<VoidAlloc>(*allocator.get());
    assigned_work_.resize(num_workers);
    for (int i = 0; i < num_workers; i++) {
      assigned_work_.push_back(std::queue<std::shared_ptr<AnyExecutable>>());
    }
  }

  explicit PrefetchMemoryStrategy(size_t num_workers = 0)
  {
    allocator_ = std::make_shared<VoidAlloc>();
    assigned_work_.resize(num_workers);
    for (int i = 0; i < num_workers; i++) {
      assigned_work_.push_back(std::queue<std::shared_ptr<AnyExecutable>>());
    }
  }

  bool collect_entities(const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) override
  {
    bool has_invalid_weak_groups_or_nodes = false;
    for (const auto & pair : weak_groups_to_nodes) {
      auto group = pair.first.lock();
      auto node = pair.second.lock();
      if (group == nullptr || node == nullptr) {
        has_invalid_weak_groups_or_nodes = true;
        continue;
      }
      if (!group || !group->can_be_taken_from().load()) {
        continue;
      }
      group->find_subscription_ptrs_if(
        [this](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
          subscription_handles_.push_back(subscription->get_subscription_handle());
          return false;
        });
      group->find_service_ptrs_if(
        [this](const rclcpp::ServiceBase::SharedPtr & service) {
          service_handles_.push_back(service->get_service_handle());
          return false;
        });
      group->find_client_ptrs_if(
        [this](const rclcpp::ClientBase::SharedPtr & client) {
          client_handles_.push_back(client->get_client_handle());
          return false;
        });
      group->find_timer_ptrs_if(
        [this](const rclcpp::TimerBase::SharedPtr & timer) {
          timer_handles_.push_back(timer->get_timer_handle());
          return false;
        });
      group->find_waitable_ptrs_if(
        [this](const rclcpp::Waitable::SharedPtr & waitable) {
          waitable_handles_.push_back(waitable);
          return false;
        });
    }

    return has_invalid_weak_groups_or_nodes;
  }

  size_t number_of_ready_subscriptions() const override
  {
    size_t number_of_subscriptions = subscription_handles_.size();
    for (auto waitable : waitable_handles_) {
      number_of_subscriptions += waitable->get_number_of_ready_subscriptions();
    }
    return number_of_subscriptions;
  }

  size_t number_of_ready_services() const override
  {
    size_t number_of_services = service_handles_.size();
    for (auto waitable : waitable_handles_) {
      number_of_services += waitable->get_number_of_ready_services();
    }
    return number_of_services;
  }

  size_t number_of_ready_events() const override
  {
    size_t number_of_events = 0;
    for (auto waitable : waitable_handles_) {
      number_of_events += waitable->get_number_of_ready_events();
    }
    return number_of_events;
  }

  size_t number_of_ready_clients() const override
  {
    size_t number_of_clients = client_handles_.size();
    for (auto waitable : waitable_handles_) {
      number_of_clients += waitable->get_number_of_ready_clients();
    }
    return number_of_clients;
  }

  size_t number_of_guard_conditions() const override
  {
    size_t number_of_guard_conditions = guard_conditions_.size();
    for (auto waitable : waitable_handles_) {
      number_of_guard_conditions += waitable->get_number_of_ready_guard_conditions();
    }
    return number_of_guard_conditions;
  }

  size_t number_of_ready_timers() const override
  {
    size_t number_of_timers = timer_handles_.size();
    for (auto waitable : waitable_handles_) {
      number_of_timers += waitable->get_number_of_ready_timers();
    }
    return number_of_timers;
  }

  size_t number_of_waitables() const override
  {
    return waitable_handles_.size();
  }

  void add_waitable_handle(const rclcpp::Waitable::SharedPtr & waitable) override
  {
    if (nullptr == waitable) {
      throw std::runtime_error("waitable object unexpectedly nullptr");
    }
    waitable_handles_.push_back(waitable);
  }

  bool add_handles_to_wait_set(rcl_wait_set_t * wait_set) override
  {
    for (auto subscription : subscription_handles_) {
      if (rcl_wait_set_add_subscription(wait_set, subscription.get(), NULL) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR_NAMED(
          "rclcpp",
          "Couldn't add subscription to wait set: %s", rcl_get_error_string().str);
        return false;
      }
    }

    for (auto client : client_handles_) {
      if (rcl_wait_set_add_client(wait_set, client.get(), NULL) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR_NAMED(
          "rclcpp",
          "Couldn't add client to wait set: %s", rcl_get_error_string().str);
        return false;
      }
    }

    for (auto service : service_handles_) {
      if (rcl_wait_set_add_service(wait_set, service.get(), NULL) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR_NAMED(
          "rclcpp",
          "Couldn't add service to wait set: %s", rcl_get_error_string().str);
        return false;
      }
    }

    for (auto timer : timer_handles_) {
      if (rcl_wait_set_add_timer(wait_set, timer.get(), NULL) != RCL_RET_OK) {
        RCUTILS_LOG_ERROR_NAMED(
          "rclcpp",
          "Couldn't add timer to wait set: %s", rcl_get_error_string().str);
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
        message.reset(&loaned_msg);    // TODO: This might be bad. After returning, the shared_ptr is
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

  void clear_handles() override
  {
    subscription_handles_.clear();
    service_handles_.clear();
    client_handles_.clear();
    timer_handles_.clear();
    waitable_handles_.clear();
  }

  void remove_null_handles(rcl_wait_set_t * wait_set) override
  {
    // TODO(jacobperron): Check if wait set sizes are what we expect them to be?
    //                    e.g. wait_set->size_of_clients == client_handles_.size()

    // Important to use subscription_handles_.size() instead of wait set's size since
    // there may be more subscriptions in the wait set due to Waitables added to the end.
    // The same logic applies for other entities.
    for (size_t i = 0; i < subscription_handles_.size(); ++i) {
      if (!wait_set->subscriptions[i]) {
        subscription_handles_[i].reset();
      }
    }
    for (size_t i = 0; i < service_handles_.size(); ++i) {
      if (!wait_set->services[i]) {
        service_handles_[i].reset();
      }
    }
    for (size_t i = 0; i < client_handles_.size(); ++i) {
      if (!wait_set->clients[i]) {
        client_handles_[i].reset();
      }
    }
    for (size_t i = 0; i < timer_handles_.size(); ++i) {
      if (!wait_set->timers[i]) {
        timer_handles_[i].reset();
      }
    }
    for (size_t i = 0; i < waitable_handles_.size(); ++i) {
      if (!waitable_handles_[i]->is_ready(wait_set)) {
        waitable_handles_[i].reset();
      }
    }

    subscription_handles_.erase(
      std::remove(subscription_handles_.begin(), subscription_handles_.end(), nullptr),
      subscription_handles_.end()
    );

    service_handles_.erase(
      std::remove(service_handles_.begin(), service_handles_.end(), nullptr),
      service_handles_.end()
    );

    client_handles_.erase(
      std::remove(client_handles_.begin(), client_handles_.end(), nullptr),
      client_handles_.end()
    );

    timer_handles_.erase(
      std::remove(timer_handles_.begin(), timer_handles_.end(), nullptr),
      timer_handles_.end()
    );

    waitable_handles_.erase(
      std::remove(waitable_handles_.begin(), waitable_handles_.end(), nullptr),
      waitable_handles_.end()
    );
  }

  void add_guard_condition(const rcl_guard_condition_t * guard_condition) override
  {
    for (const auto & existing_guard_condition : guard_conditions_) {
      if (existing_guard_condition == guard_condition) {
        return;
      }
    }
    guard_conditions_.push_back(guard_condition);
  }

  void remove_guard_condition(const rcl_guard_condition_t * guard_condition) override
  {
    for (auto it = guard_conditions_.begin(); it != guard_conditions_.end(); ++it) {
      if (*it == guard_condition) {
        guard_conditions_.erase(it);
        break;
      }
    }

    return return_val;
  }

  void
  get_next_subscription(
    rclcpp::AnyExecutable & any_exec,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) override
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
        any_exec.subscription = subscription;
        any_exec.callback_group = group;
        any_exec.node_base = get_node_by_group(group, weak_groups_to_nodes);
        // Fetch all messages awaiting this subscription
        std::shared_ptr<void> msg;

        while(get_subscription_message(msg, subscription)) { // && msg != nullptr ???
          released_work_.emplace({subscription, nullptr, nullptr, nullptr, nullptr, group,
                                  get_node_by_group(group, weak_groups_to_nodes), msg});
        }
        // Else, the subscription is no longer valid, remove it and continue
        it = subscription_handles_.erase(it);
      }
    }
  }

  void
  get_next_service(
    rclcpp::AnyExecutable & any_exec,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) override
  {
    auto it = service_handles_.begin();
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
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        any_exec.service = service;
        any_exec.callback_group = group;
        any_exec.node_base = get_node_by_group(group, weak_groups_to_nodes);
        service_handles_.erase(it);
        return;
      }
    }
  }

  void
  get_next_timer(
    rclcpp::AnyExecutable & any_exec,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) override
  {
    auto it = timer_handles_.begin();
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
        any_exec.timer = timer;
        any_exec.callback_group = group;
        any_exec.node_base = get_node_by_group(group, weak_groups_to_nodes);
        timer_handles_.erase(it);
        return;
      }
      // Else, the timer is no longer valid, remove it and continue
      it = timer_handles_.erase(it);
    }
  }

  void
  get_next_waitable(
    rclcpp::AnyExecutable & any_exec,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes) override
  {
    auto it = waitable_handles_.begin();
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
        if (!group->can_be_taken_from().load()) {
          // Group is mutually exclusive and is being used, so skip it for now
          // Leave it to be checked next time, but continue searching
          ++it;
          continue;
        }
        // Otherwise it is safe to set and return the any_exec
        any_exec.waitable = waitable;
        any_exec.callback_group = group;
        any_exec.node_base = get_node_by_group(group, weak_groups_to_nodes);
        waitable_handles_.erase(it);
        return;
      }
    }
  }

  rcl_allocator_t get_allocator() override
  {
    return rclcpp::allocator::get_rcl_allocator<void *, VoidAlloc>(*allocator_.get());
  }

  bool
  get_subscription_message(
    std::shared_ptr<void> message,
    rclcpp::SubscriptionBase::SharedPtr subscription)
  {
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
        message.reset(&loaned_msg);   // TODO(nightduck): This might be bad. After returning, the
                                      // shared_ptr is destroyed, deleting the loaned message. Maybe
                                      // give it a pointer to a pointer, like above
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
    waitable_handles_.push_back(waitable);
  }

  bool
  get_next_executable(
    rclcpp::AnyExecutable & any_exec,
    size_t worker_id
  )
  {
    // TODO(nightduck): Make atomic
    // TODO(nightduck): As is, it does not consider weak_groups_to_nodes

    // If this worker was given work, check that first
    if (!assigned_work_[worker_id].empty()) {
      any_exec = *assigned_work_[worker_id].front();
      assigned_work_[worker_id].pop();
      return true;
    }

    // If there's any high priority tasks being blocked by mutual exclusion, assign them to worker
    // that's blocking them
    while (!released_work_.empty() && !can_run(*released_work_.top())) {
      std::shared_ptr<AnyExecutable> top_ae = released_work_.top();

      for (int i = 0; i < running_work.size(); i++) {
        if (running_work[i]->callback_group == top_ae->callback_group) {
          released_work_.pop();

      // If the work couldn't run, it should now be assigned to whoever was blocking it
      assert(top_ae != released_work_.top());   // TODO(nightduck): This might be undefined if empty
    }

    // If there's tasks left, they can be run
    if (released_work_.empty()) {
      return false;
    } else {
      any_exec = *released_work_.top();
      released_work_.pop();
      return true;
    }
  }

  // TODO(nightduck): Add overloaded version which takes reference to container and Functor for
  // filtering. Useful for decentralized planning
  void
  collect_work(
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes
  )
  {
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

          while (get_subscription_message(msg, subscription)) {  // && msg != nullptr ???
            std::shared_ptr<rclcpp::AnyExecutable> ae = std::make_shared<rclcpp::AnyExecutable>();
            ae->subscription = subscription;
            ae->callback_group = group;
            ae->node_base = get_node_by_group(group, weak_groups_to_nodes);
            ae->data = msg;
            released_work_.push(ae);
          }
          subscription_handles_.erase(it);
          return;
        }
        // Else, the subscription is no longer valid, remove it and continue
        it = subscription_handles_.erase(it);
      }
    }

    {
      // Get all triggered timers
      auto it = timer_handles_.begin();
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
          std::shared_ptr<rclcpp::AnyExecutable> ae = std::make_shared<rclcpp::AnyExecutable>();
          ae->timer = timer;
          ae->callback_group = group;
          ae->node_base = get_node_by_group(group, weak_groups_to_nodes);
          released_work_.push(ae);

          timer_handles_.erase(it);
        }
        // Else, the timer is no longer valid, remove it and continue
        it = timer_handles_.erase(it);
      }
    }

    {
      // Get all services and their requests
      auto it = service_handles_.begin();
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

          while (service->take_type_erased_request(request.get(), *request_header)) {
            std::shared_ptr<rclcpp::AnyExecutable> ae = std::make_shared<rclcpp::AnyExecutable>();
            ae->service = service;
            ae->callback_group = group;
            ae->node_base = get_node_by_group(group, weak_groups_to_nodes);
            ae->data = request;
            released_work_.push(ae);
          }
          service_handles_.erase(it);
        }
        // Else, the service is no longer valid, remove it and continue
        it = service_handles_.erase(it);
      }
    }

    {
      // Get all clients and their responses
      auto it = client_handles_.begin();
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

          while (client->take_type_erased_response(response.get(), *request_header)) { \
            std::shared_ptr<rclcpp::AnyExecutable> ae = std::make_shared<rclcpp::AnyExecutable>();
            ae->client = client;
            ae->callback_group = group;
            ae->node_base = get_node_by_group(group, weak_groups_to_nodes);
            ae->data = response;
            released_work_.push(ae);
          }
          client_handles_.erase(it);
        }
        // Else, the service is no longer valid, remove it and continue
        it = client_handles_.erase(it);
      }
    }

    {
      // Get all waitables
      auto it = waitable_handles_.begin();
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
          std::shared_ptr<rclcpp::AnyExecutable> ae = std::make_shared<rclcpp::AnyExecutable>();
          ae->waitable = waitable;
          ae->callback_group = group;
          ae->node_base = get_node_by_group(group, weak_groups_to_nodes);
          released_work_.push(ae);

          waitable_handles_.erase(it);
          return;
        }
        // Else, the waitable is no longer valid, remove it and continue
        it = waitable_handles_.erase(it);
      }
    }
  }

  bool can_run(const AnyExecutable & any_exec)
  {
    return any_exec.callback_group->can_be_taken_from().load();
  }

protected:
  Adaptor released_work_;

  // TODO(nightduck): Incorporate this into generics
  std::vector<std::queue<std::shared_ptr<AnyExecutable>>> assigned_work_;
  std::vector<std::shared_ptr<AnyExecutable>> running_work;

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


  std::shared_ptr<VoidAlloc> allocator_;

  template<typename T>
  void do_nothing(T t) const {}
};

}  // namespace prefetch_memory_strategy
}  // namespace memory_strategies
}  // namespace rclcpp

#endif  // RCLCPP__STRATEGIES__PREFETCH_MEMORY_STRATEGY_HPP_
