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
#include "rclcpp/experimental/graph_executable.hpp"

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

  GraphExecutor executor;
  EXPECT_EQ(executor.get_graph_nodes().size(), 0u);

  // Create timer, test it gets added to graph_nodes_ correctly
  auto node1 = std::make_shared<rclcpp::Node>("node1");
  auto pub1 = node1->create_publisher<test_msgs::msg::Empty>("/pub1", rclcpp::SensorDataQoS());
  auto tmr1 = node1->create_timer(1s, []() {}, nullptr, {pub1});
  rclcpp::experimental::graph_node_t::SharedPtr tmr_node;
  executor.add_node(node1);
  EXPECT_EQ(executor.get_graph_nodes().size(), 2u);
  if (executor.get_graph_nodes().count(tmr1.get()) == 1) {
    tmr_node = executor.get_graph_nodes().find(tmr1.get())->second;
    EXPECT_EQ(tmr_node->parent, nullptr);
    EXPECT_TRUE(tmr_node->children.empty());
  } else if (executor.get_graph_nodes().count(tmr1.get()) > 1) {
    EXPECT_TRUE(false) << "Timer found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "graph_nodes_ doesn't contain timer at address " << tmr1.get();
  }

  // Create 2 subscriptions that subscribe to timer's topic, test they get added to graph_nodes_
  auto node2 = std::make_shared<rclcpp::Node>("node2");
  auto pub2 = node2->create_publisher<test_msgs::msg::Empty>("/pub2", rclcpp::SensorDataQoS());
  auto sub1 = node2->create_subscription<test_msgs::msg::Empty>(
    "/pub1", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {pub2});
  auto sub2 = node2->create_subscription<test_msgs::msg::Empty>(
    "/pub1", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {pub2});
  rclcpp::experimental::graph_node_t::SharedPtr sub1_node;
  rclcpp::experimental::graph_node_t::SharedPtr sub2_node;
  executor.add_node(node2);
  EXPECT_EQ(executor.get_graph_nodes().size(), 5u);

  if (executor.get_graph_nodes().count(sub1->get_subscription_handle().get()) == 1) {
    sub1_node = executor.get_graph_nodes().find(sub1->get_subscription_handle().get())->second;
    EXPECT_EQ(sub1_node->parent, tmr_node);
    EXPECT_TRUE(sub1_node->children.empty());
    EXPECT_EQ(sub1_node->input_topic, "/pub1");
  } else if (executor.get_graph_nodes().count(sub1->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }
  if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) == 1) {
    sub2_node = executor.get_graph_nodes().find(sub2->get_subscription_handle().get())->second;
    EXPECT_EQ(sub2_node->parent, tmr_node);
    EXPECT_TRUE(sub2_node->children.empty());
    EXPECT_EQ(sub2_node->input_topic, "/pub1");
  } else if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Create 3rd subscription that subscribes to 1st and 2nd subscriptions' topics, test it gets
  // added to graph_nodes_ twice
  auto node3 = std::make_shared<rclcpp::Node>("node3");
  auto sub3 = node3->create_subscription<test_msgs::msg::Empty>(
    "/pub2", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {});
  executor.add_node(node3);
  EXPECT_EQ(executor.get_graph_nodes().size(), 8u);

  // Check that sub3 is added twice
  if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) == 1) {
    EXPECT_TRUE(false) << "Subscription found in graph_nodes_ only once";
  } else if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub3->get_subscription_handle().get());
    auto sub3_node_a = it->second;
    auto sub3_node_b = (++it)->second;
    EXPECT_EQ(sub3_node_a->input_topic, "/pub2");
    EXPECT_TRUE(sub3_node_a->children.empty());
    EXPECT_EQ(sub3_node_b->input_topic, "/pub2");
    EXPECT_TRUE(sub3_node_b->children.empty());
    if (sub3_node_a->parent != sub1_node) {
      EXPECT_EQ(sub3_node_a->parent, sub2_node);
      EXPECT_EQ(sub3_node_b->parent, sub1_node) << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub3_node_a->parent, sub1_node);
      EXPECT_EQ(sub3_node_b->parent, sub2_node) << "Duplicated subscription doesn't have a different parent";
    }
    // Check that each parent is pointing at the child pointing at it
    auto parent_a_vec = sub3_node_a->parent->children;
    auto parent_b_vec = sub3_node_b->parent->children;
    EXPECT_TRUE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub3_node_a) != parent_a_vec.end());
    EXPECT_TRUE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub3_node_b) != parent_b_vec.end());
    EXPECT_FALSE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub3_node_b) != parent_a_vec.end());
    EXPECT_FALSE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub3_node_a) != parent_b_vec.end());
  } else if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) > 2) {
    EXPECT_TRUE(false) << "Subscription found more than twice in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Create 4th subscription that's dangling, test it gets added to graph_nodes_ correctly
  auto node4 = std::make_shared<rclcpp::Node>("node4");
  auto sub4 = node4->create_subscription<test_msgs::msg::Empty>(
    "/pub3", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {});
  rclcpp::experimental::graph_node_t::SharedPtr sub4_node;
  executor.add_node(node4);

  EXPECT_EQ(executor.get_graph_nodes().size(), 10u);
  if (executor.get_graph_nodes().count(sub4->get_subscription_handle().get()) == 1) {
    sub4_node = executor.get_graph_nodes().find(sub4->get_subscription_handle().get())->second;
    EXPECT_EQ(sub4_node->parent, nullptr);
    EXPECT_TRUE(sub4_node->children.empty());
    EXPECT_EQ(sub4_node->input_topic, "/pub3");
  } else if (executor.get_graph_nodes().count(sub4->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Create 5th subscription that subscribes to 1st and 2nd subscriptions' topics, and publishes to
  // 4th subscription's topic, test it gets added to graph_nodes_ twice, and downstream subscription
  // is duplicated as well
  auto node5 = std::make_shared<rclcpp::Node>("node5");
  auto pub3 = node5->create_publisher<test_msgs::msg::Empty>("/pub3", rclcpp::SensorDataQoS());
  auto sub5 = node5->create_subscription<test_msgs::msg::Empty>(
    "/pub2", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {pub3});
  executor.add_node(node5);

  rclcpp::experimental::graph_node_t::SharedPtr sub5_node_a;
  rclcpp::experimental::graph_node_t::SharedPtr sub5_node_b;
  EXPECT_EQ(executor.get_graph_nodes().size(), 14u);
  if (executor.get_graph_nodes().count(sub5->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub5->get_subscription_handle().get());
    sub5_node_a = it->second;
    sub5_node_b = (++it)->second;
    EXPECT_EQ(sub5_node_a->input_topic, "/pub2");
    EXPECT_EQ(sub5_node_b->input_topic, "/pub2");
    if (sub5_node_a->parent != sub1_node) {
      EXPECT_EQ(sub5_node_a->parent, sub2_node);
      EXPECT_EQ(sub5_node_b->parent, sub1_node) << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub5_node_a->parent, sub1_node);
      EXPECT_EQ(sub5_node_b->parent, sub2_node) << "Duplicated subscription doesn't have a different parent";
    }

    // Check that each parent is pointing at the child pointing at it
    auto parent_a_vec = sub5_node_a->parent->children;
    auto parent_b_vec = sub5_node_b->parent->children;
    EXPECT_TRUE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub5_node_a) != parent_a_vec.end());
    EXPECT_TRUE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub5_node_b) != parent_b_vec.end());
    EXPECT_FALSE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub5_node_b) != parent_a_vec.end());
    EXPECT_FALSE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub5_node_a) != parent_b_vec.end());
  } else {
    EXPECT_EQ(executor.get_graph_nodes().count(sub5->get_subscription_handle().get()), 2) << "Subscription not in graph nodes twice";
  }

  if (executor.get_graph_nodes().count(sub4->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub4->get_subscription_handle().get());
    auto sub4_node_a = it->second;
    auto sub4_node_b = (++it)->second;
    EXPECT_EQ(sub4_node_a->input_topic, "/pub3");
    EXPECT_EQ(sub4_node_b->input_topic, "/pub3");
    if (sub4_node_a->parent != sub5_node_a) {
      EXPECT_EQ(sub4_node_a->parent, sub5_node_b);
      EXPECT_EQ(sub4_node_b->parent, sub5_node_a) << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub4_node_a->parent, sub5_node_a);
      EXPECT_EQ(sub4_node_b->parent, sub5_node_b) << "Duplicated subscription doesn't have a different parent";
    }

    // Check that each parent is pointing at the child pointing at it
    auto parent_a_vec = sub4_node_a->parent->children;
    auto parent_b_vec = sub4_node_b->parent->children;
    EXPECT_TRUE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub4_node_a) != parent_a_vec.end());
    EXPECT_TRUE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub4_node_b) != parent_b_vec.end());
    EXPECT_FALSE(std::find(parent_a_vec.begin(), parent_a_vec.end(), sub4_node_b) != parent_a_vec.end());
    EXPECT_FALSE(std::find(parent_b_vec.begin(), parent_b_vec.end(), sub4_node_a) != parent_b_vec.end());
  } else {
    EXPECT_EQ(executor.get_graph_nodes().count(sub4->get_subscription_handle().get()), 2) << "Child subscription not duplicated";
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