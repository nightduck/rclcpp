// Copyright 2014 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__EXECUTORS__GENERIC_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__GENERIC_EXECUTOR_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "rclcpp/executor.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/memory_strategies.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/strategies/prefetch_memory_strategy.hpp"

namespace rclcpp
{
namespace executors
{

// TODO: Enforce Adaptor uses correct parameters in its own template: https://www.informit.com/articles/article.aspx?p=376878
template<
  class MemStrat = rclcpp::memory_strategies::prefetch_memory_strategy::PrefetchMemoryStrategy<>
>
class GenericExecutor : public rclcpp::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(GenericExecutor)

  // Type checking
  static_assert(
    std::is_base_of<memory_strategies::prefetch_memory_strategy::PrefetchMemoryStrategy<>, MemStrat>::value,
    "Template parameter must be of PrefetchMemoryStrategy type"
  );

  /// Constructor for GenericExecutor.
  /**
   * For the yield_before_execute option, when true std::this_thread::yield()
   * will be called after acquiring work (as an AnyExecutable) and
   * releasing the spinning lock, but before executing the work.
   * This is useful for reproducing some bugs related to taking work more than
   * once.
   *
   * \param context the execution context that would normally be put in ExecutorOptions
   * \param max_conditions max conditions that would normally be put in ExecutorOptions
   * \param number_of_threads number of threads to have in the thread pool,
   *   the default 0 will use the number of cpu cores found instead
   * \param yield_before_execute if true std::this_thread::yield() is called
   * \param timeout maximum time to wait
   */
  RCLCPP_PUBLIC
  GenericExecutor(
    const rclcpp::Context::SharedPtr context = rclcpp::contexts::get_global_default_context(),
    size_t max_conditions = 0,
    size_t number_of_threads = 0,
    bool yield_before_execute = false,
    std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));

  RCLCPP_PUBLIC
  GenericExecutor(
    size_t number_of_threads,
    bool yield_before_execute = false,
    std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));

  RCLCPP_PUBLIC
  virtual ~GenericExecutor();

  /**
   * \sa rclcpp::Executor:spin() for more details
   * \throws std::runtime_error when spin() called while already spinning
   */
  RCLCPP_PUBLIC
  void
  spin() override;

  RCLCPP_PUBLIC
  size_t
  get_number_of_threads();

  /**
  * \throws std::runtime_error if the wait set can be cleared
  */
  RCLCPP_PUBLIC
  void
  wait_for_work(std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));

  RCLCPP_PUBLIC
  bool
  get_next_ready_executable(AnyExecutable & any_executable);

protected:
  RCLCPP_PUBLIC
  void
  run(size_t this_thread_number);

private:
  RCLCPP_DISABLE_COPY(GenericExecutor)

  bool
  get_next_ready_executable_from_map(
    AnyExecutable & any_executable,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes);

  // // TODO: Custom allocator here to avoid dynamic memory operations?
  // typedef std::queue<rclcpp::AnyExecutable>
  //   ReleasedWorkQueue;

  // ReleasedWorkQueue
  // released_work_ RCPPUTILS_TSA_GUARDED_BY(mutex_);

  // // TODO: This just hide the base member, which will still be used by all methods not overriden
  // /// The memory strategy: an interface for handling user-defined memory allocation strategies.
  // memory_strategies::allocator_memory_strategy::PrefetchMemoryStrategy<std::allocator<void>>
  // memory_strategy_ RCPPUTILS_TSA_PT_GUARDED_BY(mutex_);

  std::mutex wait_mutex_;
  size_t number_of_threads_;
  bool yield_before_execute_;
  std::chrono::nanoseconds next_exec_timeout_;
};


//// Implementation

template<class MemStrat>
GenericExecutor<MemStrat>::GenericExecutor(
  const rclcpp::Context::SharedPtr context,
  size_t max_conditions,
  size_t number_of_threads,
  bool yield_before_execute,
  std::chrono::nanoseconds next_exec_timeout)
: rclcpp::Executor(rclcpp::ExecutorOptions(std::make_shared<MemStrat>(), context, max_conditions)),
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
  ((MemStrat)memory_strategy_)->collect_work();
}

template<class MemStrat>
bool
GenericExecutor<MemStrat>::get_next_ready_executable(
  AnyExecutable & any_executable)
{
  return ((MemStrat)memory_strategy_)->get_next_executable(any_executable);
}

template<class MemStrat>
void
GenericExecutor<MemStrat>::run(size_t)
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


}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__GENERIC_EXECUTOR_HPP_
