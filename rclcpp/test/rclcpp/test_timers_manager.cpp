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

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <utility>

#include "rclcpp/contexts/default_context.hpp"
#include "rclcpp/experimental/timers_manager.hpp"

using namespace std::chrono_literals;

using rclcpp::experimental::TimersManager;

using CallbackT = std::function<void ()>;
using TimerT = rclcpp::WallTimer<CallbackT>;

class TestTimersManager : public ::testing::Test
{
public:
  void SetUp()
  {
    rclcpp::init(0, nullptr);
  }

  void TearDown()
  {
    rclcpp::shutdown();
  }
};

static void execute_all_ready_timers(std::shared_ptr<TimersManager> timers_manager)
{
  bool head_was_ready = false;
  do {
    head_was_ready = timers_manager->execute_head_timer();
  } while (head_was_ready);
}

TEST_F(TestTimersManager, empty_manager)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  EXPECT_EQ(std::chrono::nanoseconds::max(), timers_manager->get_head_timeout());
  EXPECT_FALSE(timers_manager->execute_head_timer());
  EXPECT_NO_THROW(timers_manager->clear());
}

TEST_F(TestTimersManager, add_run_remove_timer)
{
  size_t t_runs = 0;
  std::chrono::milliseconds timer_period(10);

  auto t = TimerT::make_shared(
    timer_period,
    [&t_runs]() {
      t_runs++;
    },
    rclcpp::contexts::get_global_default_context());
  std::weak_ptr<TimerT> t_weak = t;

  // Add the timer to the timers manager
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());
  timers_manager->add_timer(t);

  // Sleep for more 3 times the timer period
  std::this_thread::sleep_for(3 * timer_period);

  // The timer is executed only once, even if we slept 3 times the period
  execute_all_ready_timers(timers_manager);
  EXPECT_EQ(1u, t_runs);

  // Remove the timer from the manager
  timers_manager->remove_timer(t);

  t.reset();
  // The timer is now not valid anymore
  EXPECT_FALSE(t_weak.lock() != nullptr);
}

TEST_F(TestTimersManager, clear)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  auto t1 = TimerT::make_shared(1ms, CallbackT(), rclcpp::contexts::get_global_default_context());
  std::weak_ptr<TimerT> t1_weak = t1;
  auto t2 = TimerT::make_shared(1ms, CallbackT(), rclcpp::contexts::get_global_default_context());
  std::weak_ptr<TimerT> t2_weak = t2;

  timers_manager->add_timer(t1);
  timers_manager->add_timer(t2);

  EXPECT_TRUE(t1_weak.lock() != nullptr);
  EXPECT_TRUE(t2_weak.lock() != nullptr);

  timers_manager->clear();

  t1.reset();
  t2.reset();

  EXPECT_FALSE(t1_weak.lock() != nullptr);
  EXPECT_FALSE(t2_weak.lock() != nullptr);
}

TEST_F(TestTimersManager, remove_not_existing_timer)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  // Try to remove a nullptr timer
  EXPECT_NO_THROW(timers_manager->remove_timer(nullptr));

  auto t = TimerT::make_shared(1ms, CallbackT(), rclcpp::contexts::get_global_default_context());
  timers_manager->add_timer(t);

  // Remove twice the same timer
  timers_manager->remove_timer(t);
  EXPECT_NO_THROW(timers_manager->remove_timer(t));
}

TEST_F(TestTimersManager, add_timer_twice)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  auto t = TimerT::make_shared(1ms, CallbackT(), rclcpp::contexts::get_global_default_context());

  timers_manager->add_timer(t);
  EXPECT_NO_THROW(timers_manager->add_timer(t));
}

TEST_F(TestTimersManager, add_nullptr)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  EXPECT_THROW(timers_manager->add_timer(nullptr), std::exception);
}

TEST_F(TestTimersManager, head_not_ready)
{
  auto timers_manager = std::make_shared<TimersManager>(
    rclcpp::contexts::get_global_default_context());

  size_t t_runs = 0;
  auto t = TimerT::make_shared(
    10s,
    [&t_runs]() {
      t_runs++;
    },
    rclcpp::contexts::get_global_default_context());

  timers_manager->add_timer(t);

  // Timer will take 10s to get ready, so nothing to execute here
  bool ret = timers_manager->execute_head_timer();
  EXPECT_FALSE(ret);
  EXPECT_EQ(0u, t_runs);
}

TEST_F(TestTimersManager, destructor)
{
  size_t t_runs = 0;
  auto t = TimerT::make_shared(
    1ms,
    [&t_runs]() {
      t_runs++;
    },
    rclcpp::contexts::get_global_default_context());
  std::weak_ptr<TimerT> t_weak = t;

  // When the timers manager is destroyed, it will stop the thread
  // and clear the timers
  {
    auto timers_manager = std::make_shared<TimersManager>(
      rclcpp::contexts::get_global_default_context());

    timers_manager->add_timer(t);
  }

  // The thread is not running anymore, so this value does not increase
  t.reset();
  EXPECT_FALSE(t_weak.lock() != nullptr);
}