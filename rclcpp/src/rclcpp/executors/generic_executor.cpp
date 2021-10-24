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

#include "rclcpp/executors/generic_executor.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "rcpputils/scope_exit.hpp"

#include "rclcpp/utilities.hpp"

using rclcpp::executors::GenericExecutor;

template<class MemStrat>
GenericExecutor<MemStrat>::GenericExecutor(
  const rclcpp::Context::SharedPtr context,
  size_t max_conditions,
  size_t number_of_threads,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: rclcpp::Executor(rclcpp::ExecutorOptions(MemStrat(), context, max_conditions)),
  yield_before_execute_(yield_before_execute),
  next_exec_timeout_(next_exec_timeout)
{
  number_of_threads_ = number_of_threads ? number_of_threads : std::thread::hardware_concurrency();
  if (number_of_threads_ == 0) {
    number_of_threads_ = 1;
  }
}

template<class MemStrat>
GenericExecutor<MemStrat>::GenericExecutor(
  size_t number_of_threads,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: GenericExecutor<MemStrat>(
      rclcpp::contexts::get_global_default_context(),
      0, number_of_threads, yield_before_execute, next_exec_timeout)
{}

template<class MemStrat>
GenericExecutor<MemStrat>::~GenericExecutor() {}

// This method is defined as an example. It will function as a decentralized non-preemptive
// scheduler. 
template<class MemStrat>
void
GenericExecutor<MemStrat>::spin()
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
      auto func = std::bind(&GenericExecutor::run, this, thread_id);
      threads.emplace_back(func);
    }
  }

  run(thread_id);
  for (auto & thread : threads) {
    thread.join();
  }
}

template<class MemStrat>
size_t
GenericExecutor<MemStrat>::get_number_of_threads()
{
  return number_of_threads_;
}

template<class MemStrat>
void
GenericExecutor<MemStrat>::wait_for_work(std::chrono::nanoseconds timeout)
{
  Executor::wait_for_work(timeout);
  ((MemStrat)memory_strategy_)->collect_work(weak_groups_to_nodes_);
}

template<class MemStrat>
bool
GenericExecutor<MemStrat>::get_next_ready_executable(
  AnyExecutable & any_executable, size_t this_thread_number)
{
  return ((MemStrat)memory_strategy_)->get_next_executable(any_executable);
}

template<class MemStrat>
bool
GenericExecutor<MemStrat>::get_next_executable(AnyExecutable & any_executable,
        std::chrono::nanoseconds timeout, size_t this_thread_number)
{
  bool success = false;
  // Check to see if there are any subscriptions or timers needing service
  // TODO(wjwwood): improve run to run efficiency of this function
  success = get_next_ready_executable(any_executable, this_thread_number);
  // If there are none
  if (!success) {
    // Wait for subscriptions or timers to work on
    wait_for_work(timeout);
    if (!spinning.load()) {
      return false;
    }
    // Try again
    success = get_next_ready_executable(any_executable, this_thread_number);
  }
  return success;
}

template<class MemStrat>
void
GenericExecutor<MemStrat>::run(size_t this_thread_number)
{
  while (rclcpp::ok(this->context_) && spinning.load()) {
    rclcpp::AnyExecutable any_exec;
    {
      std::lock_guard wait_lock{wait_mutex_};
      if (!rclcpp::ok(this->context_) || !spinning.load()) {
        return;
      }
      if (!get_next_executable(any_exec, next_exec_timeout_, this_thread_number)) {
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
