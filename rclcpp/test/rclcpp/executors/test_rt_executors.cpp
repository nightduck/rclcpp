// Copyright 2018 Open Source Robotics Foundation, Inc.
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
#include <string>
#include <memory>

#include "rclcpp/exceptions.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/executors.hpp"

#include "rclcpp/strategies/allocator_memory_strategy.hpp"


#include "std_msgs/msg/int32.hpp"

using namespace std::chrono_literals;

class TestRTExecutors : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }
};

constexpr std::chrono::milliseconds PERIOD_MS = 1000ms;
constexpr double PERIOD = PERIOD_MS.count() / 1000.0;
constexpr double TOLERANCE = PERIOD / 4.0;

// /*
//    Test that timers are not taken multiple times when using reentrant callback groups.
//  */
// TEST_F(TestRTExecutors, timer_over_take) {
// #ifdef __linux__
//   // This seems to be the most effective way to force the bug to happen on Linux.
//   // This is unnecessary on MacOS, since the default scheduler causes it.
//   struct sched_param param;
//   param.sched_priority = 0;
//   if (sched_setscheduler(0, SCHED_BATCH, &param) != 0) {
//     perror("sched_setscheduler");
//   }
// #endif

//   bool yield_before_execute = true;

//   rclcpp::executors::FixedPrioExecutor executor([](rclcpp::AnyExecutable) {return 50;});

//   std::shared_ptr<rclcpp::Node> node =
//     std::make_shared<rclcpp::Node>("test_multi_threaded_executor_timer_over_take");

//   auto cbg = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

//   rclcpp::Clock system_clock(RCL_STEADY_TIME);
//   std::mutex last_mutex;
//   auto last = system_clock.now();

//   std::atomic_int timer_count {0};

//   auto timer_callback = [&timer_count, &executor, &system_clock, &last_mutex, &last]() {
//       // While this tolerance is a little wide, if the bug occurs, the next step will
//       // happen almost instantly. The purpose of this test is not to measure the jitter
//       // in timers, just assert that a reasonable amount of time has passed.
//       rclcpp::Time now = system_clock.now();
//       timer_count++;

//       if (timer_count > 5) {
//         executor.cancel();
//       }

//       {
//         std::lock_guard<std::mutex> lock(last_mutex);
//         double diff = static_cast<double>(std::abs((now - last).nanoseconds())) / 1.0e9;
//         last = now;

//         if (diff < PERIOD - TOLERANCE) {
//           executor.cancel();
//           ASSERT_GT(diff, PERIOD - TOLERANCE);
//         }
//       }
//     };

//   auto timer = node->create_wall_timer(PERIOD_MS, timer_callback, cbg);
//   executor.add_node(node);
//   executor.spin();
// }

// TEST_F(TestRTExecutors, subscription_spam) {
//   rclcpp::executors::FixedPrioExecutor executor([](rclcpp::AnyExecutable){return 50;});

//   ASSERT_GT(executor.get_number_of_threads(), 1u);

//   std::shared_ptr<rclcpp::Node> ping_node =
//     std::make_shared<rclcpp::Node>("ping_node");
//   std::shared_ptr<rclcpp::Node> pong_node =
//     std::make_shared<rclcpp::Node>("pong_node");

//   auto ping_pub = ping_node->create_publisher<std_msgs::msg::Int32>("ping", 10);
//   auto pong_pub = pong_node->create_publisher<std_msgs::msg::Int32>("pong", 10);

//   rclcpp::Clock system_clock(RCL_STEADY_TIME);
//   std::atomic_int ping_timer_count {0};
//   std::atomic_int pong_timer_count {0};

//   auto ping_tmr_callback = [&ping_timer_count, &system_clock, &ping_pub]() {
//     ping_timer_count++;

//     rclcpp::Time start = system_clock.now();

//     while (system_clock.now() - start < rclcpp::Duration::from_nanoseconds(100000000));

//     auto msg = std::make_unique<std_msgs::msg::Int32>();
//     msg->data = ping_timer_count;
//     ping_pub->publish(std::move(msg));
//   };
//   auto pong_tmr_callback = [&pong_timer_count, &system_clock, &pong_pub]() {
//     pong_timer_count++;

//     rclcpp::Time start;

//     for (int i = 0; i < 10; i++)
//     {
//       start = system_clock.now();
//       while(system_clock.now() - start < rclcpp::Duration::from_nanoseconds(10000000));

//       auto msg = std::make_unique<std_msgs::msg::Int32>();
//       msg->data = pong_timer_count;
//       pong_pub->publish(std::move(msg));
//     }


//   };
//   auto ping_sub_callback = []() {

//   };
//   auto pong_sub_callback = []() {

//   };
// }

// TODO(nightduck): Test two subscriptions in same cbg don't run at same time
// TODO(nightduck): Test two subscriptions in diff cbg run at same time
// TODO(nightduck): Test two subscriptions in same reenrant cbg run at same time
