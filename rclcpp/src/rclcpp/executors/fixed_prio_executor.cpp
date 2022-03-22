// Copyright 2019 Nobleo Technology
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

#include "rclcpp/executors/fixed_prio_executor.hpp"

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "rcpputils/scope_exit.hpp"

using rclcpp::executors::FixedPrioExecutor;
using rclcpp::experimental::ExecutableList;
using namespace std::placeholders;  // for _1, _2, _3...
using namespace std::chrono_literals;

FixedPrioExecutor::FixedPrioExecutor(
  std::function<int(rclcpp::AnyExecutable)> predicate,
  const rclcpp::ExecutorOptions & options,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: StaticSingleThreadedExecutor(options),
  yield_before_execute_(yield_before_execute),
  next_exec_timeout_(next_exec_timeout)
{
  prio_function = predicate;
}

FixedPrioExecutor::~FixedPrioExecutor()
{}

void
FixedPrioExecutor::map_execs_to_groups()
{
  // Make mapping between executables and callback groups
  sub_to_group_map.clear();
  tmr_to_group_map.clear();
  client_to_group_map.clear();
  service_to_group_map.clear();
  waitable_to_group_map.clear();
  std::vector<rclcpp::CallbackGroup::WeakPtr> groups =
    entities_collector_->get_all_callback_groups();
  std::for_each(
    groups.begin(), groups.end(),
    [this](rclcpp::CallbackGroup::WeakPtr group_ptr) {
      auto group = group_ptr.lock();
      group->find_timer_ptrs_if(
        [this, &group_ptr](const rclcpp::TimerBase::SharedPtr & timer) {
          if (timer) {
            this->tmr_to_group_map.insert({timer, group_ptr});
          }
          return false;
        });
      group->find_subscription_ptrs_if(
        [this, &group_ptr](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
          if (subscription) {
            this->sub_to_group_map.insert({subscription, group_ptr});
          }
          return false;
        });
      group->find_service_ptrs_if(
        [this, &group_ptr](const rclcpp::ServiceBase::SharedPtr & service) {
          if (service) {
            this->service_to_group_map.insert({service, group_ptr});
          }
          return false;
        });
      group->find_client_ptrs_if(
        [this, &group_ptr](const rclcpp::ClientBase::SharedPtr & client) {
          if (client) {
            this->client_to_group_map.insert({client, group_ptr});
          }
          return false;
        });
      group->find_waitable_ptrs_if(
        [this, &group_ptr](const rclcpp::Waitable::SharedPtr & waitable) {
          if (waitable) {
            this->waitable_to_group_map.insert({waitable, group_ptr});
          }
          return false;
        });
    });
}

void
FixedPrioExecutor::spin()
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  // Set memory_strategy_ and exec_list_ based on weak_nodes_
  // Prepare wait_set_ based on memory_strategy_
  if (!entities_collector_->is_init()) {
    entities_collector_->init(&wait_set_, memory_strategy_);
  }

  map_execs_to_groups();

  while (rclcpp::ok(this->context_) && spinning.load()) {
    // Refresh wait set and wait for work
    entities_collector_->refresh_wait_set();
    execute_ready_executables();
  }
}

void
FixedPrioExecutor::spin_some(std::chrono::nanoseconds max_duration)
{
  // In this context a 0 input max_duration means no duration limit
  if (std::chrono::nanoseconds(0) == max_duration) {
    max_duration = std::chrono::nanoseconds::max();
  }

  return this->spin_some_impl(max_duration, false);
}

void
FixedPrioExecutor::spin_all(std::chrono::nanoseconds max_duration)
{
  if (max_duration <= 0ns) {
    throw std::invalid_argument("max_duration must be positive");
  }
  return this->spin_some_impl(max_duration, true);
}

void
FixedPrioExecutor::spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive)
{
  // Make sure the entities collector has been initialized
  if (!entities_collector_->is_init()) {
    entities_collector_->init(&wait_set_, memory_strategy_);
  }

  auto start = std::chrono::steady_clock::now();
  auto max_duration_not_elapsed = [max_duration, start]() {
      if (std::chrono::nanoseconds(0) == max_duration) {
        // told to spin forever if need be
        return true;
      } else if (std::chrono::steady_clock::now() - start < max_duration) {
        // told to spin only for some maximum amount of time
        return true;
      }
      // spun too long
      return false;
    };

  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  map_execs_to_groups();

  while (rclcpp::ok(context_) && spinning.load() && max_duration_not_elapsed()) {
    // Get executables that are ready now
    entities_collector_->refresh_wait_set(std::chrono::milliseconds::zero());
    // Execute ready executables
    bool work_available = execute_ready_executables();
    if (!work_available || !exhaustive) {
      break;
    }
  }
}

void
FixedPrioExecutor::allocate_cbg_resources(rclcpp::CallbackGroup::SharedPtr cbg)
{
  // Create thread and data for it to operate on. If allowed to run, it'll immediately sleep
  auto cw = std::make_shared<rclcpp::experimental::CBG_Work>();
  cw->thread = std::thread(std::bind(&FixedPrioExecutor::run, this, _1), cw);

  // Make it high priority, to guarantee responsiveness when it wakes up
  struct sched_param p;
  p.sched_priority = sched_get_priority_max(SCHED_FIFO);
  pthread_setschedparam(cw->thread.native_handle(), SCHED_FIFO, &p);

  std::lock_guard<std::mutex> lk(wait_mutex_);
  cbg_threads.emplace(cbg, cw);
}

void
FixedPrioExecutor::add_callback_group(
  rclcpp::CallbackGroup::SharedPtr group_ptr,
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
  bool notify)
{
  if (group_ptr->type() != rclcpp::CallbackGroupType::MutuallyExclusive) {
    printf("ERROR: Executor only supports mutually exclusive executors at the moment\n");
    return;
  }
  allocate_cbg_resources(group_ptr);

  StaticSingleThreadedExecutor::add_callback_group(group_ptr, node_ptr, notify);
}

void
FixedPrioExecutor::add_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  bool is_reentrant = false;
  node_ptr->for_each_callback_group(
    [&is_reentrant](rclcpp::CallbackGroup::SharedPtr cbg) {
      is_reentrant = is_reentrant | (cbg->type() != rclcpp::CallbackGroupType::MutuallyExclusive);
    }
  );
  if (is_reentrant) {
    printf("ERROR: Executor only supports mutually exclusive executors at the moment\n");
    throw rclcpp::exceptions::InvalidNodeError();
  }

  node_ptr->for_each_callback_group(
    std::bind(
      &FixedPrioExecutor::allocate_cbg_resources, this,
      _1));

  StaticSingleThreadedExecutor::add_node(node_ptr, notify);
}

void
FixedPrioExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->add_node(node_ptr->get_node_base_interface(), notify);
}


void
FixedPrioExecutor::run(rclcpp::experimental::CBG_Work::SharedPtr work)
{
  do {
    // TODO(nightduck): Make some way for below function to unblock when stopped spinning
    auto exec = work->get_work();

    pthread_setschedprio(work->thread.native_handle(), work->priority);

    if (exec->timer) {
      TRACEPOINT(
        rclcpp_executor_execute,
        static_cast<const void *>(exec->timer->get_timer_handle().get()));
      exec->timer->execute_callback();
    }
    if (exec->subscription) {
      TRACEPOINT(
        rclcpp_executor_execute,
        static_cast<const void *>(exec->subscription->get_subscription_handle().get()));
      rclcpp::MessageInfo message_info;
      message_info.get_rmw_message_info().from_intra_process = false;
      if (exec->subscription->is_serialized()) {
        auto serialized_msg = std::static_pointer_cast<SerializedMessage>(exec->data);
        exec->subscription->handle_serialized_message(serialized_msg, message_info);
        exec->subscription->return_serialized_message(serialized_msg);
      } else if (exec->subscription->can_loan_messages()) {
        exec->subscription->handle_loaned_message(exec->data.get(), message_info);
        rcl_ret_t ret = rcl_return_loaned_message_from_subscription(
          exec->subscription->get_subscription_handle().get(), exec->data.get());
        if (RCL_RET_OK != ret) {
          RCLCPP_ERROR(
            rclcpp::get_logger(
              "rclcpp"),
            "rcl_return_loaned_message_from_subscription() failed for subscription on topic "
            "'%s': %s",
            exec->subscription->get_topic_name(), rcl_get_error_string().str);
        }
        exec->data = nullptr;
      } else {
        exec->subscription->handle_message(exec->data, message_info);
        exec->subscription->return_message(exec->data);
      }
    }
    if (exec->service) {
      execute_service(exec->service);
    }
    if (exec->client) {
      execute_client(exec->client);
    }
    if (exec->waitable) {
      exec->waitable->execute(exec->data);
    }

    pthread_setschedprio(work->thread.native_handle(), sched_get_priority_max(SCHED_FIFO));
  } while (rclcpp::ok(this->context_) && spinning.load());
}

bool
FixedPrioExecutor::execute_ready_executables(bool spin_once)
{
  bool any_ready_executable = false;

  // Put all the ready subscriptions and the messages into the correct thread
  for (size_t i = 0; i < wait_set_.size_of_subscriptions; ++i) {
    if (i < entities_collector_->get_number_of_subscriptions()) {
      if (wait_set_.subscriptions[i]) {
        execute_subscription(entities_collector_->get_subscription(i));
        if (spin_once) {
          return true;
        }
        any_ready_executable = true;
      }
    }
  }
  // Put all the ready timers and the messages into the correct thread
  for (size_t i = 0; i < wait_set_.size_of_timers; ++i) {
    if (i < entities_collector_->get_number_of_timers()) {
      if (wait_set_.timers[i] && entities_collector_->get_timer(i)->is_ready()) {
        auto timer = entities_collector_->get_timer(i);
        timer->call();
        execute_timer(std::move(timer));
        if (spin_once) {
          return true;
        }
        any_ready_executable = true;
      }
    }
  }
  // Put all the ready services and the messages into the correct thread
  for (size_t i = 0; i < wait_set_.size_of_services; ++i) {
    if (i < entities_collector_->get_number_of_services()) {
      if (wait_set_.services[i]) {
        execute_service(entities_collector_->get_service(i));
        if (spin_once) {
          return true;
        }
        any_ready_executable = true;
      }
    }
  }
  // Put all the ready clients and the messages into the correct thread
  for (size_t i = 0; i < wait_set_.size_of_clients; ++i) {
    if (i < entities_collector_->get_number_of_clients()) {
      if (wait_set_.clients[i]) {
        execute_client(entities_collector_->get_client(i));
        if (spin_once) {
          return true;
        }
        any_ready_executable = true;
      }
    }
  }
  // Put all the ready waitables and the messages into the correct thread
  for (size_t i = 0; i < entities_collector_->get_number_of_waitables(); ++i) {
    auto waitable = entities_collector_->get_waitable(i);
    if (waitable->is_ready(&wait_set_)) {
      auto data = waitable->take_data();
      waitable->execute(data);
      if (spin_once) {
        return true;
      }
      any_ready_executable = true;
    }
  }
  return any_ready_executable;
}

bool
FixedPrioExecutor::get_subscription_message(
  std::shared_ptr<void> & message,
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
                                    // shared_ptr is destroyed, deleting the loaned message.
                                    // Maybe give it a pointer to a pointer, like above
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

void
FixedPrioExecutor::execute_subscription(rclcpp::SubscriptionBase::SharedPtr subscription)
{
  auto group = sub_to_group_map.at(subscription).lock();

  // Fetch all messages awaiting this subscription
  std::shared_ptr<void> msg;

  while (get_subscription_message(msg, subscription)) {  // && msg != nullptr ???
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.subscription = subscription;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = msg;

    // Get priority and add it to thread
    int prio = prio_function(ae);
    cbg_threads[group]->add_work(ae, prio);
  }
}

void
FixedPrioExecutor::execute_timer(rclcpp::TimerBase::SharedPtr timer)
{
  // Find the group for this handle and see if it can be serviced
  auto group = tmr_to_group_map.at(timer).lock();

  // Otherwise it is safe to set and return the any_exec
  rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
  ae.timer = timer;
  ae.callback_group = group;
  ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);

  // Get priority and add it to thread
  int prio = prio_function(ae);
  cbg_threads[group]->add_work(ae, prio);
}

void
FixedPrioExecutor::execute_service(rclcpp::ServiceBase::SharedPtr service)
{
  // Find the group for this handle and see if it can be serviced
  auto group = service_to_group_map.at(service).lock();

  // Otherwise it is safe to set and return the any_exec
  // Fetch all messages awaiting this service
  auto request_header = service->create_request_header();
  std::shared_ptr<void> request = service->create_request();

  while (service->take_type_erased_request(request.get(), *request_header)) {
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.service = service;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = request;

    // Get priority and add it to thread
    int prio = prio_function(ae);
    cbg_threads[group]->add_work(ae, prio);
  }
}

void
FixedPrioExecutor::execute_client(rclcpp::ClientBase::SharedPtr client)
{
  // Find the group for this handle and see if it can be serviced
  auto group = client_to_group_map.at(client).lock();

  // Otherwise it is safe to set and return the any_exec
  // Fetch all messages awaiting this client
  auto request_header = client->create_request_header();
  std::shared_ptr<void> response = client->create_response();

  while (client->take_type_erased_response(response.get(), *request_header)) {
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.client = client;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = response;

    // Get priority and add it to thread
    int prio = prio_function(ae);
    cbg_threads[group]->add_work(ae, prio);
  }
}
