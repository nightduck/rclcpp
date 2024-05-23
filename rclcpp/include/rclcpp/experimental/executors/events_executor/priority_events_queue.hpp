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

#ifndef RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__PRIORITY_EVENTS_QUEUE_HPP_
#define RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__PRIORITY_EVENTS_QUEUE_HPP_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>
#include <iostream>

#include "rclcpp/experimental/executors/events_executor/events_queue.hpp"

namespace rclcpp
{
namespace experimental
{
namespace executors
{

struct PriorityEvent
{
  int64_t priority;
  rclcpp::experimental::executors::ExecutorEvent event;
};

struct ComparePriorities : public std::binary_function<PriorityEvent, PriorityEvent, bool>
{
  _GLIBCXX14_CONSTEXPR
  bool
  operator()(const PriorityEvent & __x, const PriorityEvent & __y) const
  {return __x.priority > __y.priority;}
};

/**
 * @brief This class implements an EventsQueue as a simple wrapper around a std::priority_queue.
 * It does not perform any checks about the size of queue, which can grow
 * unbounded without being pruned.
 */
class PriorityEventsQueue : public EventsQueue
{
public:
  RCLCPP_SMART_PTR_ALIASES_ONLY(PriorityEventsQueue)

  RCLCPP_PUBLIC
  PriorityEventsQueue()
  {
    // // Default callback to extract priority from event
    // extract_priority_ = [](const rclcpp::experimental::executors::ExecutorEvent & event) {
    //     (void)(event);
    //     return 1;
    //   };
  }

  RCLCPP_PUBLIC
  ~PriorityEventsQueue() override = default;

  /**
   * @brief enqueue event into the queue
   * Thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue(const rclcpp::experimental::executors::ExecutorEvent & event) override
  {
    int64_t priority = priority_[event.entity_key];
    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      for (size_t ev = 0; ev < event.num_events; ev++) {
        event_queue_.push(single_event);
      }
    }
    events_queue_cv_.notify_one();
  }

  /**
   * @brief enqueue event into the queue without locking
   * Not thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue_unsafe(const rclcpp::experimental::executors::ExecutorEvent & event) override
  {
    int64_t priority = priority_[event.entity_key];
    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    event_queue_.push(single_event);
  }

  /**
   * @brief Lock the queue
   * This function is used to lock the queue to prevent other threads from accessing it.
   * Must be called in conjunction with queue_unlock(), otherwise all queue operations will block
  */
  RCLCPP_PUBLIC
  void
  queue_lock() override {
    mutex_.lock();
  }

  /**
   * @brief Unlock the queue
   * This function is used to unlock the queue after it has been locked with queue_lock().
   * Will notify any waiting threads that the queue is unlocked.
  */
  RCLCPP_PUBLIC
  void
  queue_unlock() override {
    mutex_.unlock();
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

    // Initialize to true because it's only needed if we have a valid timeout
    bool has_data = true;
    if (timeout != std::chrono::nanoseconds::max()) {
      has_data =
        events_queue_cv_.wait_for(lock, timeout, [this]() {return !event_queue_.empty();});
    } else {
      events_queue_cv_.wait(lock, [this]() {return !event_queue_.empty();});
    }

    if (has_data) {
      event = event_queue_.top().event;
      event_queue_.pop();
      return true;
    }

    return false;
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
    return event_queue_.empty();
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
    return event_queue_.size();
  }

  /**
   * @brief Set the priority of an entity
   * Thread safe
   * @param entity_id The entity to set the priority for
   * @param priority The priority to set
   */
  RCLCPP_PUBLIC
  void
  set_priority(const void * entity_id, int priority)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    priority_[entity_id] = priority;
  }

  /**
   * @brief Get the priority of an entity
   * Thread safe
   * @param entity_id The entity to get the priority for
   * @return The priority of the entity
   */
  RCLCPP_PUBLIC
  int
  get_priority(const void * entity_id) const
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return priority_.at(entity_id);
  }

protected:
  // // Callback to extract priority from event
  // std::function<int(const rclcpp::experimental::executors::ExecutorEvent &)> extract_priority_;
  // The underlying queue implementation
  std::priority_queue<rclcpp::experimental::executors::PriorityEvent,
    std::deque<rclcpp::experimental::executors::PriorityEvent>,
    ComparePriorities> event_queue_;
  // Map entity_id to fixed priority
  std::map<const void *, int> priority_;
  // Mutex to protect read/write access to the queue
  mutable std::mutex mutex_;
  // Variable used to notify when an event is added to the queue
  std::condition_variable events_queue_cv_;
};

class EDFEventsQueue : public PriorityEventsQueue
{
public:
  RCLCPP_SMART_PTR_ALIASES_ONLY(EDFEventsQueue)

  RCLCPP_PUBLIC
  EDFEventsQueue()
  {
    // // Default callback to extract priority from event
    // extract_priority_ = [](const rclcpp::experimental::executors::ExecutorEvent & event) {
    //     (void)(event);
    //     return 1;
    //   };
  }

  RCLCPP_PUBLIC
  ~EDFEventsQueue() override = default;

  /**
   * @brief enqueue event into the queue
   * Thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue(const rclcpp::experimental::executors::ExecutorEvent & event) override
  {
    int64_t priority;
    if (event.type == rclcpp::experimental::executors::ExecutorEventType::TIMER_EVENT) {
      priority = static_cast<const rclcpp::TimerBase *>(event.entity_key)->get_arrival_time();
    } else {
      priority = 1;
    }

    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      for (size_t ev = 0; ev < event.num_events; ev++) {
        event_queue_.push(single_event);
      }
    }
    events_queue_cv_.notify_one();
  }

  /**
   * @brief enqueue event into the queue without locking
   * Not thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue_unsafe(const rclcpp::experimental::executors::ExecutorEvent & event)
  {
    int64_t priority;
    if (event.type == rclcpp::experimental::executors::ExecutorEventType::TIMER_EVENT) {
      priority = static_cast<const rclcpp::TimerBase *>(event.entity_key)->get_arrival_time();
    } else {
      priority = 1;
    }
    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    event_queue_.push(single_event);
  }
};

class RMEventsQueue : public PriorityEventsQueue
{
public:
  RCLCPP_SMART_PTR_ALIASES_ONLY(RMEventsQueue)

  RCLCPP_PUBLIC
  RMEventsQueue()
  {
    // // Default callback to extract priority from event
    // extract_priority_ = [](const rclcpp::experimental::executors::ExecutorEvent & event) {
    //     (void)(event);
    //     return 1;
    //   };
  }

  RCLCPP_PUBLIC
  ~RMEventsQueue() override = default;

  /**
   * @brief enqueue event into the queue
   * Thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue(const rclcpp::experimental::executors::ExecutorEvent & event) override
  {
    int64_t priority;
    if (event.type == rclcpp::experimental::executors::ExecutorEventType::TIMER_EVENT) {
      priority = static_cast<const rclcpp::TimerBase *>(event.entity_key)->get_period();
    } else {
      priority = 1;
    }

    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      for (size_t ev = 0; ev < event.num_events; ev++) {
        event_queue_.push(single_event);
      }
    }
    events_queue_cv_.notify_one();
  }

  /**
   * @brief enqueue event into the queue without locking
   * Not thread safe
   * @param event The event to enqueue into the queue
   */
  RCLCPP_PUBLIC
  void
  enqueue_unsafe(const rclcpp::experimental::executors::ExecutorEvent & event)
  {
    int64_t priority;
    if (event.type == rclcpp::experimental::executors::ExecutorEventType::TIMER_EVENT) {
      priority = static_cast<const rclcpp::TimerBase *>(event.entity_key)->get_period();
    } else {
      priority = 1;
    }
    rclcpp::experimental::executors::PriorityEvent single_event = {priority, event};
    single_event.event.num_events = 1;
    event_queue_.push(single_event);
  }
};

}  // namespace executors
}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__EXECUTORS__EVENTS_EXECUTOR__PRIORITY_EVENTS_QUEUE_HPP_
