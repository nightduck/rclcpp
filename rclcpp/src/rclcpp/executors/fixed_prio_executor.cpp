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

FixedPrioExecutor::FixedPrioExecutor(
  const rclcpp::ExecutorOptions & options,
  size_t number_of_threads,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: StaticSingleThreadedExecutor(options),
  yield_before_execute_(yield_before_execute),
  next_exec_timeout_(next_exec_timeout)
{
  number_of_threads_ = number_of_threads ? number_of_threads : std::thread::hardware_concurrency();
  if (number_of_threads_ == 0) {
    number_of_threads_ = 1;
  }
}

FixedPrioExecutor::~FixedPrioExecutor()
{}

size_t
FixedPrioExecutor::get_number_of_threads()
{
  return number_of_threads_;
}

void
FixedPrioExecutor::spin_all(std::chrono::nanoseconds max_duration)
{
}

void
FixedPrioExecutor::allocate_cbg_resources(rclcpp::CallbackGroup::SharedPtr cbg) {
  // Create thread and data for it to operate on. If allowed to run, it'll immediately sleep
  auto cw = std::make_shared<rclcpp::experimental::CBG_Work>();
  cw->thread = std::thread(std::bind(&FixedPrioExecutor::run, this, _1), cw);

  // Make it high priority, to guarantee responsiveness when it wakes up
  struct sched_param p;
  p.sched_priority = sched_get_priority_max(SCHED_FIFO);
  pthread_setschedparam(cw->thread.native_handle(), SCHED_FIFO, &p);

  std::lock_guard<std::mutex> lk(wait_mutex_);
  cbg_threads.emplace(cbg, cw);

  return;
}

void
FixedPrioExecutor::add_callback_group(
  rclcpp::CallbackGroup::SharedPtr group_ptr,
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
  bool notify)
{
  if(group_ptr->type() != rclcpp::CallbackGroupType::MutuallyExclusive) {
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
    return;
  }

  node_ptr->for_each_callback_group(std::bind(&FixedPrioExecutor::allocate_cbg_resources, this, _1));

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
  while (rclcpp::ok(this->context_) && spinning.load()) {
    // TODO: Make some way for below function to unblock when stopped spinning
    auto exec = work->get_work();

    pthread_setschedprio(work->thread.native_handle(), work->priority);

    // Will call execute_timer, execute_subscription, etc appropriately.
    // Will also reset CBG's can_be_taken_from, but we don't use that in this executor
    execute_any_executable(*exec);

    pthread_setschedprio(work->thread.native_handle(), sched_get_priority_max(SCHED_FIFO));
  }
}

bool
FixedPrioExecutor::execute_ready_executables(bool spin_once)
{
  StaticSingleThreadedExecutor::execute_ready_executables(spin_once);
}

bool
FixedPrioExecutor::get_subscription_message(
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

void
FixedPrioExecutor::execute_subscription(rclcpp::SubscriptionBase::SharedPtr subscription)
{
  // Find the group for this handle
  auto group = memory_strategy_->get_group_by_subscription(subscription, weak_groups_to_nodes_);
  assert(group != nullptr);

  // Fetch all messages awaiting this subscription
  std::shared_ptr<void> msg;

  while(get_subscription_message(msg, subscription)) { // && msg != nullptr ???
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.subscription = subscription;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = msg;
    
    // TODO: Use developer specified functor
    cbg_threads[group]->add_work(ae, 50);
  }
}

void
FixedPrioExecutor::execute_timer(rclcpp::TimerBase::SharedPtr timer) {
  // Find the group for this handle and see if it can be serviced
  auto group = memory_strategy_->get_group_by_timer(timer, weak_groups_to_nodes_);
  assert(group != nullptr);

  // Otherwise it is safe to set and return the any_exec
  rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
  ae.timer = timer;
  ae.callback_group = group;
  ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);

  // TODO: Use developer specified functor
  cbg_threads[group]->add_work(ae, 50);
}

void
FixedPrioExecutor::execute_service(rclcpp::ServiceBase::SharedPtr service) {
  // Find the group for this handle and see if it can be serviced
  auto group = memory_strategy_->get_group_by_service(service, weak_groups_to_nodes_);
  assert(group != nullptr);
  
  // Otherwise it is safe to set and return the any_exec
  // Fetch all messages awaiting this service
  auto request_header = service->create_request_header();
  std::shared_ptr<void> request = service->create_request();
  
  while(service->take_type_erased_request(request.get(), *request_header)) {
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.service = service;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = request;

    // TODO: Use developer specified functor
    cbg_threads[group]->add_work(ae, 50);
  }
}

void
FixedPrioExecutor::execute_client(rclcpp::ClientBase::SharedPtr client) {
  // Find the group for this handle and see if it can be serviced
  auto group = memory_strategy_->get_group_by_client(client, weak_groups_to_nodes_);
  assert(group != nullptr);

  // Otherwise it is safe to set and return the any_exec
  // Fetch all messages awaiting this client
  auto request_header = client->create_request_header();
  std::shared_ptr<void> response = client->create_response();
  
  while(client->take_type_erased_response(response.get(), *request_header)) {
    rclcpp::AnyExecutable ae = rclcpp::AnyExecutable();
    ae.client = client;
    ae.callback_group = group;
    ae.node_base = memory_strategy_->get_node_by_group(group, weak_groups_to_nodes_);
    ae.data = response;

    // TODO: Use developer specified functor
    cbg_threads[group]->add_work(ae, 50);
  }
}