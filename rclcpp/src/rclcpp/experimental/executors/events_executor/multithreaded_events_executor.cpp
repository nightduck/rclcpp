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

#include "rclcpp/experimental/executors/events_executor/multithreaded_events_executor.hpp"

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include "rcpputils/scope_exit.hpp"

using namespace std::chrono_literals;

using rclcpp::experimental::executors::MultithreadedEventsExecutor;

MultithreadedEventsExecutor::MultithreadedEventsExecutor(
  int number_of_threads,
  rclcpp::experimental::executors::EventsQueue::UniquePtr events_queue,
  const rclcpp::ExecutorOptions & options)
: rclcpp::experimental::executors::EventsExecutor(std::move(events_queue), options),
  number_of_threads_(number_of_threads >
    0 ? number_of_threads : std::max(std::thread::hardware_concurrency(), 1U))
{
}

MultithreadedEventsExecutor::~MultithreadedEventsExecutor()
{
}

void
MultithreadedEventsExecutor::spin()
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

  // Create a pool of worker threads and associated simple queues
  std::deque<WorkerThread> workers;
  for (int i = 0; i < number_of_threads_; i++) {
    std::function<void(rclcpp::experimental::executors::ExecutorEvent)> execution_function =
      [this](rclcpp::experimental::executors::ExecutorEvent event) {
        this->execute_event(event);
      };
    workers.emplace_back(execution_function);
  }

  for (auto & worker : workers) {
    worker.start();
  }

  while (rclcpp::ok(context_) && spinning.load()) {
    // Check if any timers are ready and enqueue them here
    timers_manager_->enqueue_ready_timers_into(events_queue_);

    // Put the event into the emptiest worker thread
    ExecutorEvent event;
    bool has_event = events_queue_->dequeue(event);
    if (has_event) {
      uint32_t emptiest_size = UINT32_MAX;
      int candidate_index;
      for(int i = 0; i < workers.size(); i++) {
        if (!workers[i].has_work()) {
          candidate_index = i;
          break;
        } else if (workers[i].get_work_size() < emptiest_size) {
          emptiest_size = workers[i].get_work_size();
          candidate_index = i;
        }
      }
      workers[candidate_index].add_work(event);
    }
  }
}

void
MultithreadedEventsExecutor::spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive)
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }

  RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

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

  // Get the number of events and timers ready at start
  const size_t ready_events_at_start = events_queue_->size();
  std::atomic<size_t> executed_events = 0;
  const size_t ready_timers_at_start = timers_manager_->get_number_ready_timers();
  size_t executed_timers = 0;

  // Create a pool of worker threads and associated simple queues
  std::deque<WorkerThread> workers;
  for (int i = 0; i < number_of_threads_; i++) {
    std::function<void(rclcpp::experimental::executors::ExecutorEvent)> execution_function =
      [this](rclcpp::experimental::executors::ExecutorEvent event) {
        this->execute_event(event);
      };
    workers.emplace_back(execution_function);
  }

  for (auto & thread : workers) {
    thread.start();
  }

  while (rclcpp::ok(context_) && spinning.load() && max_duration_not_elapsed()) {
    // Execute first ready event from queue if exists
    if (exhaustive || (executed_events < ready_events_at_start)) {
      bool has_event = !events_queue_->empty();

      if (has_event) {
        ExecutorEvent event;
        bool ret = events_queue_->dequeue(event, std::chrono::nanoseconds(0));
        if (ret) {
          uint32_t emptiest_size = UINT32_MAX;
          int candidate_index;
          for(int i = 0; i < workers.size(); i++) {
            if (workers[i].has_work()) {
              break;
            } else if (workers[i].get_work_size() < emptiest_size) {
              emptiest_size = workers[i].get_work_size();
              candidate_index = i;
            }
          }
          workers[candidate_index].add_work(event);
        }
      }
    }

    // Enqueue timers for execution
    if (exhaustive || (executed_timers < ready_timers_at_start)) {
      executed_timers += timers_manager_->enqueue_ready_timers_into(events_queue_);
    }

    // If there's no more work available, exit
    if (!exhaustive && (executed_events >= ready_events_at_start) &&
      (executed_timers >= ready_timers_at_start))
    {
      break;
    }
  }
}
