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

// TEST_F(TestGraphExecutor, graph_executable) {

// }

TEST_F(TestGraphExecutor, add_unrelated_executables)
{
  // rmw_connextdds doesn't support events-executor
  if (std::string(rmw_get_implementation_identifier()).find("rmw_connextdds") == 0) {
    GTEST_SKIP();
  }

  // Create timer publishing to 3 subscriptions
  auto node1 = std::make_shared<rclcpp::Node>("node1");
  auto node2 = std::make_shared<rclcpp::Node>("node2");
  GraphExecutor executor;
  EXPECT_EQ(executor.get_graph_nodes().size(), 0u);

  // Create 1 subscription, test it gets added to graph_nodes_ correctly
  auto sub = node1->create_subscription<test_msgs::msg::Empty>(
    "sub_topic", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {});
  executor.add_node(node1);

  // One we added, one for parameter_events, which is automatically generated for each node
  EXPECT_EQ(executor.get_graph_nodes().size(), 2u);
  if (executor.get_graph_nodes().count(sub->get_subscription_handle().get()) == 1) {
    EXPECT_EQ(
      executor.get_graph_nodes().find(
        sub->get_subscription_handle().get())->second->parent, nullptr);
    EXPECT_TRUE(
      executor.get_graph_nodes().find(
        sub->get_subscription_handle().get())->second->children.empty());
    EXPECT_EQ(
      executor.get_graph_nodes().find(
        sub->get_subscription_handle().get())->second->input_topic, "sub_topic");
  } else if (executor.get_graph_nodes().count(sub->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Create unrelated timer, test it gets added to graph_nodes_ correctly
  auto tmr = node2->create_timer(1s, []() {});
  executor.add_node(node2);
  
  // One we added, one for parameter_events, which is automatically generated for each node
  EXPECT_EQ(executor.get_graph_nodes().size(), 4u);
  if (executor.get_graph_nodes().count(tmr.get()) == 1) {
    EXPECT_EQ(executor.get_graph_nodes().find(tmr.get())->second->parent, nullptr);
    EXPECT_TRUE(executor.get_graph_nodes().find(tmr.get())->second->children.empty());
  } else if (executor.get_graph_nodes().count(tmr.get()) > 1) {
    EXPECT_TRUE(false) << "Timer found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "graph_nodes_ doesn't contain timer at address " << tmr.get();
  }

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

TEST_F(TestGraphExecutor, add_parent_executables)
{
  // rmw_connextdds doesn't support events-executor
  if (std::string(rmw_get_implementation_identifier()).find("rmw_connextdds") == 0) {
    GTEST_SKIP();
  }

  // Create timer publishing to 3 subscriptions
  auto node = std::make_shared<rclcpp::Node>("node");
  GraphExecutor executor;

  // Create 1 subscription, test it gets added to graph_nodes_ correctly

  // Add 2nd subscription that publishes to 1st subscription's topic, test that it's added

  // Create timer that publishes to 2nd subscription's topic, test it gets added normally

  // Create 3rd subscription that publishes to 2nd subscription's topic, test it gets added and
  // downstream executables are duplicated

  // Create 2nd timer that publishes to 2nd subscription's topic, test it gets added and that
  // downstream executables are duplicated

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

TEST_F(TestGraphExecutor, add_child_executables)
{
  // rmw_connextdds doesn't support events-executor
  if (std::string(rmw_get_implementation_identifier()).find("rmw_connextdds") == 0) {
    GTEST_SKIP();
  }

  // Create timer publishing to 3 subscriptions
  auto node = std::make_shared<rclcpp::Node>("node");
  GraphExecutor executor;
  executor.get_graph_nodes();

  // Create timer, test it gets added to graph_nodes_ correctly

  // Create 1st subscription that subscribes to timer's topic, test it gets added to graph_nodes_

  // Create 2nd subscription that subscribes to timer's topic, test it gets added to graph_nodes_

  // Create 3rd subscription that subscribes to 1st and 2nd subscriptions' topics, test it gets
  // added to graph_nodes_ twice

  // Create 4th subscription that's dangling, test it gets added to graph_nodes_ correctly

  // Create 5th subscription that subscribes to 1st and 2nd subscriptions' topics, and publishes to
  // 4th subscription's topic, test it gets added to graph_nodes_ twice, and downstream subscription
  // is duplicated as well

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

// Subscription conditional paths:
// x1. Subscription has no relation to existing nodes : insert normally
// x2. Subscription is child of existing node : insert normally, w/ relation to parent
// x3. Subscription is child of multiple existing nodes: duplicate child node
// x4. Subscription is parent of existing orphan node: insert w/ relation to parent
// x5. Subscription is parent of existing node: duplicate child node and downstream

// Timer conditional paths:
// Always gets inserted once
// x1. Timer has no relation to existing nodes
// x2. Timer is parent of existing orphan node: insert normally, w/ relation to parent
// x3. Timer is parent of existing node: duplicate child node and downstream nodes