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

  GraphExecutor executor;
  EXPECT_EQ(executor.get_graph_nodes().size(), 0u);

  // Create 2 subscriptions, one that publishes to the other
  auto node1 = std::make_shared<rclcpp::Node>("node1");
  auto pub1 = node1->create_publisher<test_msgs::msg::Empty>("/pub1", rclcpp::SensorDataQoS());
  auto sub1 = node1->create_subscription<test_msgs::msg::Empty>(
    "/pub1", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {});
  auto sub2 = node1->create_subscription<test_msgs::msg::Empty>(
    "/pub2", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {pub1});
  executor.add_node(node1);

  // Executor should add 3 entities: the 2 subscriptions and the parameter events sub
  // Sub2 is parent to sub2, sub1 is parentless for now
  rclcpp::experimental::graph_node_t::SharedPtr sub1_node;
  rclcpp::experimental::graph_node_t::SharedPtr sub2_node;
  EXPECT_EQ(executor.get_graph_nodes().size(), 3u);
  if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) == 1) {
    sub2_node = executor.get_graph_nodes().find(sub2->get_subscription_handle().get())->second;
    EXPECT_EQ(sub2_node->parent, nullptr);        // Sub1 is orphaned
    EXPECT_EQ(sub2_node->input_topic, "/pub2");   // Sub1 has correct input topic
  } else if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }
  if (executor.get_graph_nodes().count(sub1->get_subscription_handle().get()) == 1) {
    sub1_node = executor.get_graph_nodes().find(sub1->get_subscription_handle().get())->second;
    EXPECT_EQ(sub1_node->parent, sub2_node);      // Sub1 is child of sub2
    EXPECT_TRUE(sub1_node->children.empty());     // Sub1 is childless
                                                  // Sub1 is in sub2's children
    EXPECT_TRUE(sub2_node->children.find(sub1_node->key) != sub2_node->children.end());
    EXPECT_EQ(sub1_node->input_topic, "/pub1");   // Sub1 has correct input topic
  } else if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Create timer that publishes to lead subscription's topic
  auto node2 = std::make_shared<rclcpp::Node>("node2");
  auto pub2_a = node2->create_publisher<test_msgs::msg::Empty>("/pub2", rclcpp::SensorDataQoS());
  auto tmr1 = node2->create_timer(1s, []() {}, nullptr, {pub2_a});
  executor.add_node(node2);

  // Executor should add 2 entities: the timer and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 5u);
  rclcpp::experimental::graph_node_t::SharedPtr tmr1_node;
  if (executor.get_graph_nodes().count(tmr1.get()) == 1) {
    tmr1_node = executor.get_graph_nodes().find(tmr1.get())->second;
    EXPECT_EQ(tmr1_node->parent, nullptr);    // Tmr1 is orphaned
                                              // Sub2 is in tmr1's children
    EXPECT_TRUE(tmr1_node->children.find(sub2_node->key) != tmr1_node->children.end());
    EXPECT_TRUE(
      std::find(
        // "/pub2" is in tmr1's output_topics
        tmr1_node->output_topics.begin(), tmr1_node->output_topics.end(), "/pub2") !=
      tmr1_node->output_topics.end());
    EXPECT_EQ(tmr1_node->input_topic, "");  // Sub1 has correct input topic
  } else if (executor.get_graph_nodes().count(tmr1.get()) > 1) {
    EXPECT_TRUE(false) << "Timer found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Timer not found in graph_nodes_";
  }
  // Confirm downstream executables are not duplicated yet
  EXPECT_EQ(executor.get_graph_nodes().count(sub1->get_subscription_handle().get()), 1);
  EXPECT_EQ(executor.get_graph_nodes().count(sub2->get_subscription_handle().get()), 1);

  // Create 3rd subscription that publishes to lead subscription's topic, test it gets added and
  // downstream executables are duplicated
  auto node3 = std::make_shared<rclcpp::Node>("node3");
  auto pub2_b = node3->create_publisher<test_msgs::msg::Empty>("/pub2", rclcpp::SensorDataQoS());
  auto sub3 = node3->create_subscription<test_msgs::msg::Empty>(
    "/pub3", rclcpp::SensorDataQoS(),
    [](test_msgs::msg::Empty::ConstSharedPtr msg) {},
    {pub2_b});
  executor.add_node(node3);

  // Executor should add 4 entities: sub3, copies of sub1 and sub2, and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 9u);
  rclcpp::experimental::graph_node_t::SharedPtr sub3_node;
  if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) == 1) {
    sub3_node = executor.get_graph_nodes().find(sub3->get_subscription_handle().get())->second;
    EXPECT_EQ(sub3_node->parent, nullptr);        // sub3 is orphaned
                                                  // Sub2 is in sub3's children
    EXPECT_TRUE(sub3_node->children.find(sub2_node->key) != sub3_node->children.end());
    EXPECT_TRUE(
      std::find(
        // "/pub2" is in tmr1's output_topics
        sub3_node->output_topics.begin(), sub3_node->output_topics.end(), "/pub2") !=
      sub3_node->output_topics.end());
    EXPECT_EQ(sub3_node->input_topic, "/pub3");   // Sub3 has correct input topic
  } else if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }

  // Confirm downstream executables are duplicated
  EXPECT_EQ(executor.get_graph_nodes().count(sub1->get_subscription_handle().get()), 2);
  EXPECT_EQ(executor.get_graph_nodes().count(sub2->get_subscription_handle().get()), 2);

  // Create 2nd timer that publishes to 2nd subscription's topic, test it gets added and that
  // downstream executables are duplicated
  auto node4 = std::make_shared<rclcpp::Node>("node4");
  auto pub2_c = node4->create_publisher<test_msgs::msg::Empty>("/pub2", rclcpp::SensorDataQoS());
  auto tmr2 = node4->create_timer(1s, []() {}, nullptr, {pub2_c});
  executor.add_node(node4);

  // Executor should add 4 entities: tmr2, copies of sub2 and sub3, and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 13u);
  rclcpp::experimental::graph_node_t::SharedPtr tmr2_node;
  if (executor.get_graph_nodes().count(tmr2.get()) == 1) {
    tmr2_node = executor.get_graph_nodes().find(tmr2.get())->second;
    EXPECT_EQ(tmr2_node->parent, nullptr);    // Tmr2 is orphaned
                                              // Sub3 is in tmr2's children
    EXPECT_TRUE(tmr2_node->children.find(sub2_node->key) != tmr2_node->children.end());
    EXPECT_TRUE(
      std::find(
        // "/pub2" is in tmr1's output_topics
        tmr2_node->output_topics.begin(), tmr2_node->output_topics.end(), "/pub2") !=
      tmr2_node->output_topics.end());
    EXPECT_EQ(tmr2_node->input_topic, "");    // Sub1 has correct input topic
  } else if (executor.get_graph_nodes().count(tmr2.get()) > 1) {
    EXPECT_TRUE(false) << "TImer found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Timer not found in graph_nodes_";
  }

  // Confirm downstream executables are duplicated
  EXPECT_EQ(executor.get_graph_nodes().count(sub1->get_subscription_handle().get()), 3);
  EXPECT_EQ(executor.get_graph_nodes().count(sub2->get_subscription_handle().get()), 3);

  // Confirm that each parent has a different copy of the same children and grandchildren
  EXPECT_TRUE(tmr1_node->children[sub2_node->key] != sub3_node->children[sub2_node->key]);
  EXPECT_TRUE(sub3_node->children[sub2_node->key] != tmr2_node->children[sub2_node->key]);
  EXPECT_TRUE(tmr2_node->children[sub2_node->key] != tmr1_node->children[sub2_node->key]);
  EXPECT_TRUE(
    tmr1_node->children[sub2_node->key]->children[sub1_node->key] !=
    sub3_node->children[sub2_node->key]->children[sub1_node->key]);
  EXPECT_TRUE(
    sub3_node->children[sub2_node->key]->children[sub1_node->key] !=
    tmr2_node->children[sub2_node->key]->children[sub1_node->key]);
  EXPECT_TRUE(
    tmr2_node->children[sub2_node->key]->children[sub1_node->key] !=
    tmr1_node->children[sub2_node->key]->children[sub1_node->key]);

  // Be sure parents and children point at each other
  EXPECT_EQ(tmr1_node->children[sub2_node->key]->parent, tmr1_node);
  EXPECT_EQ(tmr2_node->children[sub2_node->key]->parent, tmr2_node);
  EXPECT_EQ(sub3_node->children[sub2_node->key]->parent, sub3_node);
  EXPECT_EQ(
    tmr1_node->children[sub2_node->key]->children[sub1_node->key]->parent,
    tmr1_node->children[sub2_node->key]);
  EXPECT_EQ(
    tmr2_node->children[sub2_node->key]->children[sub1_node->key]->parent,
    tmr2_node->children[sub2_node->key]);
  EXPECT_EQ(
    sub3_node->children[sub2_node->key]->children[sub1_node->key]->parent,
    sub3_node->children[sub2_node->key]);

  // Verify input topics of child copies were copied correctly
  auto sub2a = tmr1_node->children[sub2_node->key];
  auto sub2b = sub3_node->children[sub2_node->key];
  auto sub2c = tmr2_node->children[sub2_node->key];
  EXPECT_EQ(sub2a->input_topic, "/pub2");
  EXPECT_EQ(sub2b->input_topic, "/pub2");
  EXPECT_EQ(sub2c->input_topic, "/pub2");

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
  // Executor should have 2 entities: the timer and the parameter events sub that every node gets
  EXPECT_EQ(executor.get_graph_nodes().size(), 2u);
  if (executor.get_graph_nodes().count(tmr1.get()) == 1) {
    // Verify that the timer is added and is orphaned and childless
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

  // Executor should add 3 entities: the 2 subscriptions and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 5u);

  if (executor.get_graph_nodes().count(sub1->get_subscription_handle().get()) == 1) {
    sub1_node = executor.get_graph_nodes().find(sub1->get_subscription_handle().get())->second;
    EXPECT_EQ(sub1_node->parent, tmr_node);       // Sub linked to timer
    EXPECT_TRUE(sub1_node->children.empty());     // Sub is childless
    EXPECT_EQ(sub1_node->input_topic, "/pub1");   // Sub has correct input topic
  } else if (executor.get_graph_nodes().count(sub1->get_subscription_handle().get()) > 1) {
    EXPECT_TRUE(false) << "Subscription found more than once in graph_nodes_";
  } else {
    EXPECT_TRUE(false) << "Subscription not found in graph_nodes_";
  }
  if (executor.get_graph_nodes().count(sub2->get_subscription_handle().get()) == 1) {
    sub2_node = executor.get_graph_nodes().find(sub2->get_subscription_handle().get())->second;
    EXPECT_EQ(sub2_node->parent, tmr_node);       // Sub linked to timer
    EXPECT_TRUE(sub2_node->children.empty());     // Sub is childless
    EXPECT_EQ(sub2_node->input_topic, "/pub1");   // Sub has correct input topic
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

  // Executor should add 3 entities: the 2 copies of the subscription and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 8u);

  // Check that sub3 is added twice
  if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) == 1) {
    EXPECT_TRUE(false) << "Subscription found in graph_nodes_ only once";
  } else if (executor.get_graph_nodes().count(sub3->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub3->get_subscription_handle().get());
    auto sub3_node_a = it->second;                  // Get pointer to both copies of sub3
    auto sub3_node_b = (++it)->second;
    EXPECT_EQ(sub3_node_a->input_topic, "/pub2");   // Both copies have correct input topic
    EXPECT_EQ(sub3_node_b->input_topic, "/pub2");
    EXPECT_TRUE(sub3_node_a->children.empty());     // Both copies are childless
    EXPECT_TRUE(sub3_node_b->children.empty());
    if (sub3_node_a->parent != sub1_node) {         // Both copies have different parents
      EXPECT_EQ(sub3_node_a->parent, sub2_node);
      EXPECT_EQ(sub3_node_b->parent, sub1_node)
        << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub3_node_a->parent, sub1_node);
      EXPECT_EQ(sub3_node_b->parent, sub2_node)
        << "Duplicated subscription doesn't have a different parent";
    }
    // Check that each parent is pointing at the child pointing at it
    auto parent_a_children = sub3_node_a->parent->children;
    auto parent_b_children = sub3_node_b->parent->children;
    EXPECT_EQ(parent_a_children[sub3_node_a->key], sub3_node_a);
    EXPECT_EQ(parent_b_children[sub3_node_b->key], sub3_node_b);
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

  // Executor should add 2 entities: the subscription and the parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 10u);
  if (executor.get_graph_nodes().count(sub4->get_subscription_handle().get()) == 1) {
    sub4_node = executor.get_graph_nodes().find(sub4->get_subscription_handle().get())->second;
    EXPECT_EQ(sub4_node->parent, nullptr);        // Sub is orphaned
    EXPECT_TRUE(sub4_node->children.empty());     // Sub is childless
    EXPECT_EQ(sub4_node->input_topic, "/pub3");   // Sub has correct input topic
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

  // Executor should add 4 entities: the 2 copies of the subscription, a duplicate of sub4, and the
  // parameter events sub
  EXPECT_EQ(executor.get_graph_nodes().size(), 14u);
  rclcpp::experimental::graph_node_t::SharedPtr sub5_node_a;
  rclcpp::experimental::graph_node_t::SharedPtr sub5_node_b;
  if (executor.get_graph_nodes().count(sub5->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub5->get_subscription_handle().get());
    sub5_node_a = it->second;                       // Get pointer to both copies of sub5
    sub5_node_b = (++it)->second;
    EXPECT_EQ(sub5_node_a->input_topic, "/pub2");   // Both copies have correct input topic
    EXPECT_EQ(sub5_node_b->input_topic, "/pub2");
    if (sub5_node_a->parent != sub1_node) {         // Both copies have different parents
      EXPECT_EQ(sub5_node_a->parent, sub2_node);
      EXPECT_EQ(sub5_node_b->parent, sub1_node)
        << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub5_node_a->parent, sub1_node);
      EXPECT_EQ(sub5_node_b->parent, sub2_node)
        << "Duplicated subscription doesn't have a different parent";
    }

    // Check that each parent is pointing at the child pointing at it
    auto parent_a_children = sub5_node_a->parent->children;
    auto parent_b_children = sub5_node_b->parent->children;
    EXPECT_EQ(parent_a_children[sub5_node_a->key], sub5_node_a);
    EXPECT_EQ(parent_b_children[sub5_node_b->key], sub5_node_b);
  } else {
    EXPECT_EQ(executor.get_graph_nodes().count(sub5->get_subscription_handle().get()), 2)
      << "Subscription not in graph nodes twice";
  }

  if (executor.get_graph_nodes().count(sub4->get_subscription_handle().get()) == 2) {
    auto it = executor.get_graph_nodes().find(sub4->get_subscription_handle().get());
    auto sub4_node_a = it->second;                  // Get pointer to both copies of sub4
    auto sub4_node_b = (++it)->second;
    EXPECT_EQ(sub4_node_a->input_topic, "/pub3");   // Both copies have correct input topic
    EXPECT_EQ(sub4_node_b->input_topic, "/pub3");
    EXPECT_TRUE(sub4_node_a->children.empty());     // Both copies are childless
    EXPECT_TRUE(sub4_node_b->children.empty());
    if (sub4_node_a->parent != sub5_node_a) {       // Both copies have different parents
      EXPECT_EQ(sub4_node_a->parent, sub5_node_b);
      EXPECT_EQ(sub4_node_b->parent, sub5_node_a)
        << "Duplicated subscription doesn't have a different parent";
    } else {
      EXPECT_EQ(sub4_node_a->parent, sub5_node_a);
      EXPECT_EQ(sub4_node_b->parent, sub5_node_b)
        << "Duplicated subscription doesn't have a different parent";
    }

    // Check that each parent is pointing at the child pointing at it
    auto parent_a_children = sub4_node_a->parent->children;
    auto parent_b_children = sub4_node_b->parent->children;
    EXPECT_EQ(parent_a_children[sub4_node_a->key], sub4_node_a);
    EXPECT_EQ(parent_b_children[sub4_node_b->key], sub4_node_b);
  } else {
    EXPECT_EQ(executor.get_graph_nodes().count(sub4->get_subscription_handle().get()), 2)
      << "Child subscription not duplicated";
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
