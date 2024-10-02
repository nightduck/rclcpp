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

#ifndef RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__DEADLINE_EVENTS_QUEUE_HPP_
#define RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__DEADLINE_EVENTS_QUEUE_HPP_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stack>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/experimental/executors/events_executor/events_queue.hpp"

namespace rclcpp
{
namespace experimental
{
namespace executors
{

struct PriorityEvent
{
  int priority;
  rclcpp::experimental::executors::ExecutorEvent event;
};

struct ComparePriorities : public std::binary_function<PriorityEvent, PriorityEvent, bool>
{
  bool
  operator()(const PriorityEvent & __x, const PriorityEvent & __y) const
  {return __x.priority > __y.priority;}
};

/**
 * @brief This implements a priority queue with separate queues for timers and children. This
 * mathematically guarantees that the highest priority event is always dequeued first, and children
 * implicitly inherit the priority of their parent. Priority is determined by implicit deadlines
 * (ie the next release of a timer)
 */
class DeadlineEventsQueue : public EventsQueue
{
public:
  RCLCPP_PUBLIC
  ~DeadlineEventsQueue() override = default;

  /**
   * @brief enqueue event into the queue
   * Thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue(const rclcpp::experimental::executors::ExecutorEvent & event) override
  {
    rclcpp::experimental::executors::ExecutorEvent single_event = event;
    single_event.num_events = 1;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (single_event.type == rclcpp::experimental::executors::ExecutorEventType::TIMER_EVENT) {
        int64_t deadline = static_cast<const rclcpp::TimerBase*>(single_event.entity_key)->get_arrival_time();
        PriorityEvent priority_event = {deadline, single_event};
        for (size_t ev = 0; ev < event.num_events; ev++) {
          timers_queue_.push(priority_event);
        }
      } else {
        for (size_t ev = 0; ev < event.num_events; ev++) {
          children_queue_.push(single_event);
        }
      }
    }
    events_queue_cv_.notify_one();
  }

  /**
   * @brief waits for an event until timeout, gets a single event
   * Thread safe
   * @return true if event, false if timeout
   */
  RCLCPP_PUBLIC
  bool
  dequeue(
    rclcpp::experimental::executors::ExecutorEvent & event,
    std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max()) override
  {
    std::unique_lock<std::mutex> lock(mutex_);

    // All newly added non-root subtasks inherit the priority of the last dequeued element
    // They are guaranteed to be children of the last dequeued element on uniprocessor
    while(children_queue_.size() > children_priority_queue_.size()) {
      children_priority_queue_.push(last_priority_);
    }

    // Compare the priority of the top element in the timers queue with the top element in the
    // children queue. Return the one with the highest priority (smallest value).
    if (!timers_queue_.empty() &&
        (children_priority_queue_.empty() ||
        (timers_queue_.top().priority < children_priority_queue_.top()))) {
      last_priority_ = timers_queue_.top().priority;
      event = timers_queue_.top().event;
      timers_queue_.pop();
      return true;
    } else if (!children_queue_.empty()) {
      last_priority_ = children_priority_queue_.top();
      event = children_queue_.top();
      children_queue_.pop();
      children_priority_queue_.pop();
      return true;
    } else {
      return false;
    }
  }

  /**
   * @brief Test whether queue is empty
   * Thread safe
   * @return true if the queue's size is 0, false otherwise.
   */
  RCLCPP_PUBLIC
  bool
  empty() const override
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return timers_queue_.empty() && children_queue_.empty();
  }

  /**
   * @brief Returns the number of elements in the queue.
   * Thread safe
   * @return the number of elements in the queue.
   */
  RCLCPP_PUBLIC
  size_t
  size() const override
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return timers_queue_.size() + children_queue_.size();
  }

private:
  // Callback to extract priority from event
  std::function<int(const ExecutorEvent &)> extract_priority_;
  // The underlying queue implementation for root subtasks
  std::priority_queue<PriorityEvent,
    std::vector<PriorityEvent>,
    ComparePriorities> timers_queue_;
  // The underlying queue implementation for child subtasks
  std::stack<rclcpp::experimental::executors::ExecutorEvent> children_queue_;
  // Store priority values for elements in children_queue_
  std::stack<uint32_t> children_priority_queue_;
  // Priority/period of the last element to be dequeued
  uint32_t last_priority_ = 0;
  // Mutex to protect read/write access to the queue
  mutable std::mutex mutex_;
  // Variable used to notify when an event is added to the queue
  std::condition_variable events_queue_cv_;
};

}  // namespace executors
}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__DEADLINE_EVENTS_QUEUE_HPP_
