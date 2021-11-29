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

#ifndef RCLCPP__EXECUTORS__FIXED_PRIO_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__FIXED_PRIO_EXECUTOR_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <deque>
#include <queue>
#include <utility>
#include <algorithm>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "rclcpp/executor.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/memory_strategies.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/executors/static_single_threaded_executor.hpp"

namespace rclcpp
{

namespace experimental
{

class ComparePrio
{
public:
  using eq = std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>;
  bool operator()(const eq & lhs, const eq & rhs) const
  {
    if (lhs->empty()) {
      return false;
    } else if (rhs->empty()) {
      return true;
    } else {
      return lhs->begin()->first > rhs->begin()->first;
    }
  }
};

class CBG_Work
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(CBG_Work)

  CBG_Work()
  : priority(sched_get_priority_max(SCHED_FIFO)), heap(), sub_dict(), tmr_dict(), wait_dict()
  {}

  ~CBG_Work()
  {
    if (thread.joinable()) {
      stop_thread();
    }
    sub_dict.clear();
    tmr_dict.clear();
    heap.clear();
  }

  // TODO(nightduck): Modify this when changing to rbtree of min-max heaps
  // Put exec in heap, and return true if success (log n, wc n on number of exec types in CBG)
  bool add_work(rclcpp::AnyExecutable & exec, int prio)
  {
    {
      std::lock_guard<std::mutex> lk(mux);

      std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>> q = NULL;
      if (exec.subscription != NULL) {
        // Check if entry exist in dict
        auto h = sub_dict.find(exec.subscription);
        if (h == sub_dict.end()) {
          // If not, create empty deque, put it in dict
          auto ret = sub_dict.emplace(
            exec.subscription,
            std::make_shared<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>());
          q = ret.first->second;
        } else {
          q = h->second;
        }
      } else if (exec.timer != NULL) {
        // Check if entry exist in dict, if not, create an entry for it
        auto h = tmr_dict.find(exec.timer);
        if (h == tmr_dict.end()) {
          // If not, create empty deque, put it in dict
          auto ret = tmr_dict.emplace(
            exec.timer,
            std::make_shared<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>());
          q = ret.first->second;
        } else {
          q = h->second;
        }
      } else if (exec.client != NULL) {
        assert(false);
      } else if (exec.service != NULL) {
        assert(false);
      } else if (exec.waitable != NULL) {
        // Check if entry exist in dict, if not, create an entry for it
        auto h = wait_dict.find(exec.waitable);
        if (h == wait_dict.end()) {
          // If not, create empty deque, put it in dict
          auto ret = wait_dict.emplace(
            exec.waitable,
            std::make_shared<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>());
          q = ret.first->second;
        } else {
          q = h->second;
        }
      } else {
        assert(false);
      }

      // If an entry for this exists (or was just created above), add this message to it (1)
      q->push_back({prio, std::make_shared<rclcpp::AnyExecutable>(exec)});

      // NOTE/TODO(nightduck): Priority inheritance for subscriptions? If they have to be executed
      // in order, then they should be guaranteed to have uniform priority, or older messages
      // should inherit priority of previous messages, maybe iterate through deque with a max
      // operation?

      // If the queue was empty before now, put it in the heap (log n, wc n)
      if (q->size() == 1) {
        heap.insert(q);
      }

      priority = std::max(prio, priority);
    }

    cond.notify_one();
    return true;
  }

  // TODO(nightduck): Modify this when changing to rbtree of min-max heaps
  // Replace running with most urgent task in heap. Update priority.
  // If no work is available, sleep on conditional lock until there is
  // (log n, wc n on number of exec types in CBG)
  std::shared_ptr<rclcpp::AnyExecutable> get_work()
  {
    std::unique_lock<std::mutex> lk(mux);

    cond.wait(
      lk, [this] {return !heap.empty() || stopped;}
    );

    if (stopped) {
      return nullptr;
    }

    priority = heap.begin()->get()->front().first;
    running = heap.begin()->get()->front().second;

    // Remove running from the highest priority queue (1)
    heap.begin()->get()->pop_front();

    // Remove queue from heap (log n, wc n)
    auto dq = *heap.begin();
    heap.erase(heap.begin());

    // And if it's not empty, put it back in (this ensures RR between same-priority execs)
    // (log n, wc n)
    if (!dq->empty()) {
      heap.insert(dq);
    }

    // // Update queues
    // if (running->subscription != NULL) {
    //   auto h = sub_dict.find(running->subscription);
    //   assert(h != sub_dict.end());  // If subscription has message in heap, then it should have
    //                                 //at least an empty deque in the sub dictionary

    //   if (!h->second->empty()) {
    //     // If deque isn't empty, put it back in heap. It won't necessarily go back to the top.
    //     // This means all max prio executables get to run in round-robin
    //     heap.insert(h->second);
    //   }
    // } else if (running->timer != NULL) {
    //   auto h = tmr_dict.find(running->timer);
    //   assert(h != tmr_dict.end());  // If subscription has message in heap, then it should have
    //                                 // at least an empty deque in the sub dictionary

    //   if (!h->second->empty()) {
    //     // If deque isn't empty, put it back in heap. It won't necessarily go back to the top.
    //     // This means all max prio executables get to run in round-robin
    //     heap.insert(h->second);
    //   }
    // } else {
    //   assert(false);
    // }

    lk.unlock();

    return running;
  }

  void stop_thread()
  {
    stopped = true;
    cond.notify_one();
    thread.join();

    running = nullptr;
  }

  std::mutex mux;
  std::condition_variable cond;
  std::thread thread;

  bool stopped = false;

  int priority;

  std::shared_ptr<rclcpp::AnyExecutable> running;

  // TODO(nightduck): Replace deque with minmax heap, so messages can execute out of order

  // TODO(nightduck): This is a binary search tree. It won't always be balanced, but is necessary
  // because rbtrees don't return equivalent nodes in order, ruining round-robin between subs of
  // equivalent priority. Find a data structure that maintains the benefits of BSTs and RBTs
  std::multiset<std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>,
    ComparePrio> heap;

  // Dictionaries of deques, representing backlogged work.
  std::unordered_map<rclcpp::SubscriptionBase::SharedPtr,
    std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>> sub_dict;
  std::unordered_map<rclcpp::TimerBase::SharedPtr,
    std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>> tmr_dict;
  std::unordered_map<rclcpp::Waitable::SharedPtr,
    std::shared_ptr<std::deque<std::pair<int, std::shared_ptr<rclcpp::AnyExecutable>>>>> wait_dict;
};
}  // namespace experimental

namespace executors
{

// TODO(nightduck): Enforce Adaptor uses correct parameters in its own template:
// https://www.informit.com/articles/article.aspx?p=376878
class FixedPrioExecutor : public StaticSingleThreadedExecutor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(FixedPrioExecutor)

  /// Constructor for FixedPrioExecutor.
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
  FixedPrioExecutor(
    std::function<int(rclcpp::AnyExecutable)> predicate = [](rclcpp::AnyExecutable) {return 50;},
    const rclcpp::ExecutorOptions & options = rclcpp::ExecutorOptions(),
    bool yield_before_execute = false,
    std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));

  RCLCPP_PUBLIC
  virtual ~FixedPrioExecutor();
  /// Fixed priority executor implementation of spin.
  /**
   * This function will block until work comes in, execute it, and keep blocking.
   * It will only be interrupted by a CTRL-C (managed by the global signal handler).
   * \throws std::runtime_error when spin() called while already spinning
   */
  RCLCPP_PUBLIC
  void
  spin() override;

  /// Fixed priority executor implementation of spin some
  /**
   * This non-blocking function will execute entities that
   * were ready when this API was called, until timeout or no
   * more work available. Entities that got ready while
   * executing work, won't be taken into account here.
   *
   * Example:
   *   while(condition) {
   *     spin_some();
   *     sleep(); // User should have some sync work or
   *              // sleep to avoid a 100% CPU usage
   *   }
   */
  RCLCPP_PUBLIC
  void
  spin_some(std::chrono::nanoseconds max_duration = std::chrono::nanoseconds(0)) override;

  /// Fixed priority executor implementation of spin all
  /**
   * This non-blocking function will execute entities until
   * timeout or all work has completed with no work left
   * available.
   *
   * Example:
   *   while(condition) {
   *     spin_all();
   *     sleep(); // User should have some sync work or
   *              // sleep to avoid a 100% CPU usage
   *   }
   */
  RCLCPP_PUBLIC
  void
  spin_all(std::chrono::nanoseconds max_duration) override;

  /// Add a callback group to an executor.
  /**
   * \sa rclcpp::Executor::add_callback_group
   */
  RCLCPP_PUBLIC
  void
  add_callback_group(
    rclcpp::CallbackGroup::SharedPtr group_ptr,
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Add a node to the executor.
  /**
   * \sa rclcpp::Executor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;
  
  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::StaticSingleThreadedExecutor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::StaticSingleThreadedExecutor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

protected:
  /**
   * @brief Prefetches messages for ready executables and assigns them to the relevant thread
   * @param spin_once if true executes only the first ready executable.
   * @return true if any executable was ready.
   */
  RCLCPP_PUBLIC
  bool
  execute_ready_executables(bool spin_once = false);

  RCLCPP_PUBLIC
  void
  map_execs_to_groups();

  RCLCPP_PUBLIC
  void
  spin_some_impl(std::chrono::nanoseconds max_duration, bool exhaustive);

  RCLCPP_PUBLIC
  void
  run(rclcpp::experimental::CBG_Work::SharedPtr work);

  RCLCPP_PUBLIC
  void
  allocate_cbg_resources(rclcpp::CallbackGroup::SharedPtr cbg);

  RCLCPP_PUBLIC
  bool
  get_subscription_message(
    std::shared_ptr<void> & message,
    SubscriptionBase::SharedPtr subscription);

  RCLCPP_PUBLIC
  void
  execute_subscription(rclcpp::SubscriptionBase::SharedPtr subscription);

  RCLCPP_PUBLIC
  void
  execute_timer(rclcpp::TimerBase::SharedPtr timer);

  RCLCPP_PUBLIC
  void
  execute_service(rclcpp::ServiceBase::SharedPtr service);

  RCLCPP_PUBLIC
  void
  execute_client(rclcpp::ClientBase::SharedPtr client);

private:
  RCLCPP_DISABLE_COPY(FixedPrioExecutor)

  std::mutex wait_mutex_;
  bool yield_before_execute_;
  std::chrono::nanoseconds next_exec_timeout_;
  std::unordered_map<rclcpp::CallbackGroup::SharedPtr, rclcpp::experimental::CBG_Work::SharedPtr>
  cbg_threads;

  std::unordered_map<rclcpp::SubscriptionBase::SharedPtr, rclcpp::CallbackGroup::WeakPtr>
  sub_to_group_map;
  std::unordered_map<rclcpp::TimerBase::SharedPtr, rclcpp::CallbackGroup::WeakPtr>
  tmr_to_group_map;
  std::unordered_map<rclcpp::ClientBase::SharedPtr, rclcpp::CallbackGroup::WeakPtr>
  client_to_group_map;
  std::unordered_map<rclcpp::ServiceBase::SharedPtr, rclcpp::CallbackGroup::WeakPtr>
  service_to_group_map;
  std::unordered_map<rclcpp::Waitable::SharedPtr, rclcpp::CallbackGroup::WeakPtr>
  waitable_to_group_map;

  std::function<int(rclcpp::AnyExecutable)> prio_function;
};
}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__FIXED_PRIO_EXECUTOR_HPP_
