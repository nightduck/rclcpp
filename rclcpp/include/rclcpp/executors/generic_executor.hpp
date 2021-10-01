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

#ifndef RCLCPP__EXECUTORS__FIFO_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__FIFO_EXECUTOR_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
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

template<
  typename Alloc = std::allocator<void>,
  class Container = std::vector<AnyExecutable, Alloc>,    // TODO: Can this use the same kind of allocator?
  class Compare = std::less<AnyExecutable>,
  class Adaptor = std::priority_queue<AnyExecutable, Container, Compare>,
  class MemStrat = rclcpp::memory_strategies::prefetch_memory_strategy::PrefetchMemoryStrategy<
      Alloc, Container, Compare, Adaptor>
>
class GenericExecutor : public rclcpp::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(GenericExecutor)

  /// Constructor for FifoMttExecutor.
  /**
   * For the yield_before_execute option, when true std::this_thread::yield()
   * will be called after acquiring work (as an AnyExecutable) and
   * releasing the spinning lock, but before executing the work.
   * This is useful for reproducing some bugs related to taking work more than
   * once.
   *
   * \param options common options for all executors
   * \param number_of_threads number of threads to have in the thread pool,
   *   the default 0 will use the number of cpu cores found instead
   * \param yield_before_execute if true std::this_thread::yield() is called
   * \param timeout maximum time to wait
   */
  RCLCPP_PUBLIC
  GenericExecutor(
    const rclcpp::ExecutorOptions & options,
    const rclcpp::Context::SharedPtr context = rclcpp::contexts::get_global_default_context(),
    size_t max_conditions = 0,
    size_t number_of_threads = 0,
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

  RCLCPP_PUBLIC
  bool
  get_ready_executables_from_map(
    AnyExecutable & any_executable,
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes);

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

}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__FIFO_MTT_EXECUTOR_HPP_
