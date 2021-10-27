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
#include <utility>

#include "rclcpp/exceptions.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/executors.hpp"

#include "rclcpp/strategies/allocator_memory_strategy.hpp"


#include "std_msgs/msg/int32.hpp"
#include "test_msgs/msg/empty.hpp"

using namespace std::chrono_literals;

class TestRTExecutors : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestCase()
  {
    rclcpp::shutdown();
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

// Test two subscriptions in same cbg don't run at same time (subs in node 2)
// Test two subscriptions in diff cbg run at same time (node 1 vs node 2)
TEST_F(TestRTExecutors, mutual_exclusion) {
  rclcpp::executors::FixedPrioExecutor executor([](rclcpp::AnyExecutable ae) {return 50;});

  std::shared_ptr<rclcpp::Node> node1 =
    std::make_shared<rclcpp::Node>("node1");
  std::shared_ptr<rclcpp::Node> node2 =
    std::make_shared<rclcpp::Node>("node2");

  auto pub_a = node1->create_publisher<test_msgs::msg::Empty>("topic_a", 10);
  auto pub_b = node1->create_publisher<test_msgs::msg::Empty>("topic_b", 10);

  // Node1's sub will verify this is zero before triggering node 2's subs, who both increment it.
  // Node1's sub then verifies the final value is 2, to confirm concurrency between node 1 and 2
  std::atomic_int race_var;
  race_var = 0;

  // When node2's subs wake, they also check this variable, increment it, and spin. They then
  // check the variable didn't change during spin to verify concurrency within ndoe2 didn't occur
  std::atomic_int threadsafe_var;
  threadsafe_var = 0;

  std::mutex mux;

  // This will run one, should run concurrently with one instance of above callback
  auto node1_callback =
    [&node1, &race_var, &threadsafe_var, &pub_b](test_msgs::msg::Empty::ConstSharedPtr) {
      ASSERT_EQ(race_var, 0);
      ASSERT_EQ(threadsafe_var, 0);

      test_msgs::msg::Empty msg;
      pub_b->publish(msg);

      uint64_t x = 0xDEADBEEF;
      rclcpp::Time now = node1->now();
      while (node1->now() < now + rclcpp::Duration::from_nanoseconds(50000000)) {
        x ^= node1->now().nanoseconds();
      }

      ASSERT_EQ(race_var, 2);
    };

  // This will run twice in two subscriptions, should not run concurrently
  auto node2_callback =
    [&node2, &race_var, &threadsafe_var, &mux](test_msgs::msg::Empty::ConstSharedPtr) {
      int start_val;
      {
        ASSERT_LT(threadsafe_var, 2);

        std::lock_guard<std::mutex> lock{mux};
        threadsafe_var++;
        start_val = threadsafe_var;
      }

      race_var++;

      uint64_t x = 0xDEADBEEF;
      rclcpp::Time now = node2->now();
      while (node2->now() < now + rclcpp::Duration::from_nanoseconds(10000000)) {
        x ^= node2->now().nanoseconds();
      }

      ASSERT_EQ(start_val, threadsafe_var);
    };

  auto sub1 =
    node1->create_subscription<test_msgs::msg::Empty>("topic_a", 1, std::move(node1_callback));
  auto sub2 =
    node2->create_subscription<test_msgs::msg::Empty>("topic_b", 1, std::move(node2_callback));
  auto sub3 =
    node2->create_subscription<test_msgs::msg::Empty>("topic_b", 1, std::move(node2_callback));


  // Sleep for a short time to verify executor.spin() is going, and didn't throw.
  executor.add_node(node1);
  executor.add_node(node2);
  std::thread spinner([&]() {
      executor.spin();
      // std::promise<void> promise;
      // std::future<void> future = promise.get_future();

      // executor.spin_until_future_complete(future, std::chrono::milliseconds(100));
    });

  test_msgs::msg::Empty msg;
  pub_a->publish(msg);

  std::this_thread::sleep_for(100ms);
  executor.cancel();
  spinner.join();

  executor.remove_node(node1, true);
  executor.remove_node(node2, true);

  sub1.reset();
  sub2.reset();
  sub3.reset();

  pub_a.reset();
  pub_b.reset();

  node1.reset();
  node2.reset();

  ASSERT_EQ(threadsafe_var, 2);
}

// TODO(nightduck): Test two subscriptions in same reenrant cbg run at same time

TEST_F(TestRTExecutors, priority_checking) {
  rclcpp::executors::FixedPrioExecutor executor(
    [](rclcpp::AnyExecutable ae) {
      auto msg = std::static_pointer_cast<std_msgs::msg::Int32>(ae.data);
      return msg->data;
    });
}

TEST_F(TestRTExecutors, subscription_spam) {
  rclcpp::executors::FixedPrioExecutor executor(rclcpp::ExecutorOptions(), 2);

  ASSERT_GT(executor.get_number_of_threads(), 1u);

  std::shared_ptr<rclcpp::Node> ping_node =
    std::make_shared<rclcpp::Node>("ping_node");
  std::shared_ptr<rclcpp::Node> pong_node =
    std::make_shared<rclcpp::Node>("pong_node");

  
  auto ping_pub = ping_node->create_publisher<std_msgs::msg::Int32>("ping", 10);
  auto pong_pub = pong_node->create_publisher<std_msgs::msg::Int32>("pong", 10);

  rclcpp::Clock system_clock(RCL_STEADY_TIME);
  std::atomic_int ping_timer_count {0};
  std::atomic_int pong_timer_count {0};

  auto ping_tmr_callback = [&ping_timer_count, &system_clock, &ping_pub]() {
    ping_timer_count++;

    rclcpp::Time start = system_clock.now();

    while (system_clock.now() - start < rclcpp::Duration::from_nanoseconds(100000000));

    auto msg = std::make_unique<std_msgs::msg::Int32>();
    msg->data = ping_timer_count;
    ping_pub->publish(std::move(msg));
  };
  auto pong_tmr_callback = [&pong_timer_count, &system_clock, &pong_pub]() {
    pong_timer_count++;

    rclcpp::Time start;
    
    for (int i = 0; i < 10; i++)
    {
      start = system_clock.now();
      while(system_clock.now() - start < rclcpp::Duration::from_nanoseconds(10000000));

      auto msg = std::make_unique<std_msgs::msg::Int32>();
      msg->data = pong_timer_count;
      pong_pub->publish(std::move(msg));
    }


  };
  auto ping_sub_callback = []() {

  };
  auto pong_sub_callback = []() {

  };
}