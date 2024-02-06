// Copyright 2023 Washington University in St. Louis.
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
#include <memory>
#include <string>

#include "rclcpp/experimental/executors/graph_executor.hpp"

#include "test_msgs/srv/empty.hpp"
#include "test_msgs/msg/empty.hpp"

using namespace std::chrono_literals;

using rclcpp::experimental::executors::GraphExecutor;

class TestGraphExecutor : public ::testing::Test
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

TEST_F(TestGraphExecutor, run_pub_sub)
{
  auto node = std::make_shared<rclcpp::Node>("node");

  bool msg_received = false;
  auto subscription = node->create_subscription<test_msgs::msg::Empty>(
    "topic", rclcpp::SensorDataQoS(),
    [&msg_received](test_msgs::msg::Empty::ConstSharedPtr msg)
    {
      (void)msg;
      msg_received = true;
    });

  auto publisher = node->create_publisher<test_msgs::msg::Empty>("topic", rclcpp::SensorDataQoS());

  GraphExecutor executor;
  executor.add_node(node);

  bool spin_exited = false;
  std::thread spinner([&spin_exited, &executor]() {
      executor.spin();
      spin_exited = true;
    });

  auto msg = std::make_unique<test_msgs::msg::Empty>();
  publisher->publish(std::move(msg));

  // Wait some time for the subscription to receive the message
  auto start = std::chrono::high_resolution_clock::now();
  while (
    !msg_received &&
    !spin_exited &&
    (std::chrono::high_resolution_clock::now() - start < 1s))
  {
    std::this_thread::sleep_for(25ms);
  }

  executor.cancel();
  spinner.join();
  executor.remove_node(node);

  EXPECT_TRUE(msg_received);
  EXPECT_TRUE(spin_exited);
}

TEST_F(TestGraphExecutor, priority_subs)
{
  // rmw_connextdds doesn't support events-executor
  if (std::string(rmw_get_implementation_identifier()).find("rmw_connextdds") == 0) {
    GTEST_SKIP();
  }

  // Create timer publishing to 3 subscriptions
  auto node = std::make_shared<rclcpp::Node>("node");
  GraphExecutor executor;

  // Subscription conditional paths:
  // 1. Subscription has no relation to existing nodes : insert normally
  // 2. Subscription is child of existing node : insert normally, w/ relation to parent
  // 3. Subscription is child of multiple existing nodes: duplicate child node
  // 4. Subscription is parent of existing orphan node: insert w/ relation to parent
  // 5. Subscription is parent of existing node: duplicate child node

  // Timer conditional paths:
  // Always gets inserted once
  // 1. Timer has no relation to existing nodes
  // 2. Timer is parent of existing orphan node: insert normally, w/ relation to parent
  // 3. Timer is parent of existing node: duplicate child node

  bool spin_exited = false;
  std::thread spinner([&spin_exited, &executor, this]() {
      executor.spin();
      spin_exited = true;
    });

  auto msg = std::make_unique<test_msgs::msg::Empty>();

  // Wait some time for the subscription to receive the message
  auto start = std::chrono::high_resolution_clock::now();
  while (
    !spin_exited &&
    (std::chrono::high_resolution_clock::now() - start < 1s))
  {
    auto time = std::chrono::high_resolution_clock::now() - start;
    auto time_msec = std::chrono::duration_cast<std::chrono::milliseconds>(time);
    std::this_thread::sleep_for(100ms);
  }

  executor.cancel();
  spinner.join();

  EXPECT_TRUE(spin_exited);
}
