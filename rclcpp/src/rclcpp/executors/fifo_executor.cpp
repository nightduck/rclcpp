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

#include "rclcpp/executors/fifo_executor.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "rcpputils/scope_exit.hpp"

#include "rclcpp/utilities.hpp"

using rclcpp::executors::FifoExecutor;

FifoExecutor::FifoExecutor(
  const rclcpp::ExecutorOptions & options,
  size_t number_of_threads,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: rclcpp::Executor(options),
  yield_before_execute_(yield_before_execute),
  next_exec_timeout_(next_exec_timeout)
{
  number_of_threads_ = number_of_threads ? number_of_threads : std::thread::hardware_concurrency();
  if (number_of_threads_ == 0) {
    number_of_threads_ = 1;
  }
  exec_buffer_.resize(64);
}

FifoExecutor::~FifoExecutor() {}

void
FifoExecutor::spin()
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );
  std::vector<std::thread> threads;
  size_t thread_id = 0;
  {
    std::lock_guard wait_lock{wait_mutex_};
    for (; thread_id < number_of_threads_ - 1; ++thread_id) {
      auto func = std::bind(&FifoExecutor::run, this, thread_id);
      threads.emplace_back(func);
    }
  }

  run(thread_id);
  for (auto & thread : threads) {
    thread.join();
  }
}

size_t
FifoExecutor::get_number_of_threads()
{
  return number_of_threads_;
}

void
FifoExecutor::run(size_t)
{
  while (rclcpp::ok(this->context_) && spinning.load()) {
    rclcpp::AnyExecutable any_exec;
    {
      std::lock_guard wait_lock{wait_mutex_};
      if (!rclcpp::ok(this->context_) || !spinning.load()) {
        return;
      }
      if (!get_next_executable(any_exec, next_exec_timeout_)) {
        continue;
      }
    }
    if (yield_before_execute_) {
      std::this_thread::yield();
    }

    execute_any_executable(any_exec);

    // Clear the callback_group to prevent the AnyExecutable destructor from
    // resetting the callback group `can_be_taken_from`
    any_exec.callback_group.reset();
  }
}

void
FifoExecutor::wait_for_work(std::chrono::nanoseconds timeout)
{
  Executor::wait_for_work(timeout);
  memory_strategy_->collect_work(released_work_);
}

bool
FifoExecutor::get_ready_executables_from_map(
  AnyExecutable & any_executable,
  const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes)
{
  if (released_work_.empty()) {
    return false;
  } 
  any_executable = released_work_.front();

  // At this point any_executable should be valid with either a valid subscription
  // or a valid timer, or it should be a null shared_ptr
  if (any_executable.timer || any_executable.subscription || any_executable.service
        || any_executable.client || any_executable.waitable) {    // TODO: Will this ever be false?
    rclcpp::CallbackGroup::WeakPtr weak_group_ptr = any_executable.callback_group;
    auto iter = weak_groups_to_nodes.find(weak_group_ptr);
    if (iter == weak_groups_to_nodes.end()) {
      released_work_.pop();   // TODO: Verify. If not assigned to executor, then remove?
      return false;
    }
  }

  
  // If it is valid, check to see if the group is mutually exclusive or
  // not, then mark it accordingly ..Check if the callback_group belongs to this executor
  if (any_executable.callback_group && any_executable.callback_group->type() == \
    CallbackGroupType::MutuallyExclusive)
  {
    // It should not have been taken otherwise
    assert(any_executable.callback_group->can_be_taken_from().load());
    // Set to false to indicate something is being run from this group
    // This is reset to true either when the any_executable is executed or when the
    // any_executable is destructued
    any_executable.callback_group->can_be_taken_from().store(false);

    // Return false, but leave in queue for later execution
    return false;

    // TODO: Get next available unit of execution. Which means queue might not be best idea
  }
  
  released_work_.pop();
  return true;
}
