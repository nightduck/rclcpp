// Copyright 2023 iRobot Corporation.
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

#ifndef RCLCPP__EXPERIMENTAL__EXECUTORS__MULTITHREADED_EVENTS_EXECUTOR__EVENTS_EXECUTOR_HPP_
#define RCLCPP__EXPERIMENTAL__EXECUTORS__MULTITHREADED_EVENTS_EXECUTOR__EVENTS_EXECUTOR_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <thread>
#include <vector>

#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"

namespace rclcpp
{
namespace experimental
{
namespace executors
{


/// Events executor implementation
/**
 * This executor uses an events queue and a timers manager to execute entities from its
 * associated nodes and callback groups.
 * ROS 2 entities allow to set callback functions that are invoked when the entity is triggered
 * or has work to do. The events-executor sets these callbacks such that they push an
 * event into its queue.
 *
 * This executor tries to reduce as much as possible the amount of maintenance operations.
 * This allows to use customized `EventsQueue` classes to achieve different goals such
 * as very low CPU usage, bounded memory requirement, determinism, etc.
 *
 * The executor uses a weak ownership model and it locks entities only while executing
 * their related events.
 *
 * To run this executor:
 * rclcpp::experimental::executors::EventsExecutor executor;
 * executor.add_node(node);
 * executor.spin();
 * executor.remove_node(node);
 */
class MultithreadedEventsExecutor : public rclcpp::experimental::executors::EventsExecutor
{
  friend class EventsExecutorEntitiesCollector;

public:
  RCLCPP_SMART_PTR_DEFINITIONS(MultithreadedEventsExecutor)

  class WorkerThread
  {
  public:
    WorkerThread() = delete;

    WorkerThread(
      rclcpp::experimental::executors::EventsQueue::UniquePtr events_queue,
      std::function<void(rclcpp::experimental::executors::ExecutorEvent)> execution_function)
      : has_work_(false), running_(false), events_queue_(std::move(events_queue)), 
        execution_function_(execution_function) {}

    // WorkerThread(const WorkerThread& other) = delete;

    void start() {
      running_ = true;
      thread_ = std::thread(&WorkerThread::run, this);
    }

    void stop() {
      running_ = false;
      thread_.join();
    }

    void run() {
      while (running_) {
        rclcpp::experimental::executors::ExecutorEvent event;
        has_work_ = events_queue_->dequeue(event);
        if (has_work_) {
          execution_function_(event);
        } else {
          // std::unique_lock<std::mutex> lk(m_);
          // cv_.wait(lk, [this]{return has_work_;});
        }
      }
    }

    void add_work(rclcpp::experimental::executors::ExecutorEvent event) {
      has_work_ = true;
      events_queue_->enqueue(event);
      // cv_.notify_one();
    }

    bool steal_work(rclcpp::experimental::executors::ExecutorEvent & event) {
      return events_queue_->dequeue(event);
    }

    bool has_work() {
      return has_work_;
    }

    int get_work_size() {
      return events_queue_->size();
    }

  protected:
    bool has_work_;
    bool running_;    // TODO: Change to atomic variable
    rclcpp::experimental::executors::EventsQueue::UniquePtr events_queue_;
    std::function<void(rclcpp::experimental::executors::ExecutorEvent)> execution_function_;

    std::thread thread_;
    // std::condition_variable cv_;
    // std::mutex m_;
  };

  /// Default constructor. See the default constructor for Executor.
  /**
   * \param[in] events_queue The queue used to store events.
   * thread. If false, timers are executed in the same thread as all other entities.
   * \param[in] options Options used to configure the executor.
   */
  RCLCPP_PUBLIC
   MultithreadedEventsExecutor(
    int number_of_threads = 0,
    rclcpp::experimental::executors::EventsQueue::UniquePtr events_queue = std::make_unique<
      rclcpp::experimental::executors::SimpleEventsQueue>(),
    const rclcpp::ExecutorOptions & options = rclcpp::ExecutorOptions());

  /// Default destructor.
  RCLCPP_PUBLIC
  virtual ~MultithreadedEventsExecutor();

  /// Events executor implementation of spin.
  /**
   * This function will block until work comes in, execute it, and keep blocking.
   * It will only be interrupted by a CTRL-C (managed by the global signal handler).
   * \throws std::runtime_error when spin() called while already spinning
   */
  RCLCPP_PUBLIC
  void
  spin() override;

protected:
  /// Internal implementation of spin_some
  RCLCPP_PUBLIC
  void
  spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive);

  RCLCPP_DISABLE_COPY(MultithreadedEventsExecutor)

  /// Level of parallelism
  int number_of_threads_;
};

}  // namespace executors
}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__EXECUTORS__MULTITHREADED_EVENTS_EXECUTOR__EVENTS_EXECUTOR_HPP_
