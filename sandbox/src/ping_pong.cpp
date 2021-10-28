// Copyright (c) 2020 Robert Bosch GmbH
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

#include <cinttypes>
#include <cstdlib>
#include <ctime>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/executor.hpp"
#include "rclcpp/rclcpp.hpp"

#include "sandbox/ping_node.hpp"
#include "sandbox/pong_node.hpp"
#include "sandbox/utilities.hpp"

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

using sandbox::PingNode;
using sandbox::PongNode;
using sandbox::configure_thread;
using sandbox::get_thread_time;
using sandbox::ThreadPriority;

/// The main function composes a Ping node and a Pong node in one OS process
/// and runs the experiment. See README.md for an architecture diagram.
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // Create Fifo executor
  rclcpp::executors::FixedPrioExecutor exec;

  // Create Ping node instance and add it to high-prio executor.
  auto ping_node = std::make_shared<PingNode>();
  exec.add_node(ping_node);

  // Create Pong node instance and add it the one of its callback groups
  // to the high-prio executor and the other to the low-prio executor.
  auto pong_node = std::make_shared<PongNode>();
  exec.add_callback_group(
    pong_node->get_high_prio_callback_group(), pong_node->get_node_base_interface());
  exec.add_callback_group(
    pong_node->get_low_prio_callback_group(), pong_node->get_node_base_interface());

  rclcpp::Logger logger = pong_node->get_logger();

  // Create a thread for each of the two executors ...
  auto exec_thread = std::thread(
    [&]() {
      exec.spin();
    });

  // Creating the threads immediately started them.
  // Therefore, get start CPU time of each thread now.
  nanoseconds thread_begin = get_thread_time(exec_thread);
  const std::chrono::seconds EXPERIMENT_DURATION = 10s;
  RCLCPP_INFO_STREAM(
    logger, "Running experiment from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
  std::this_thread::sleep_for(EXPERIMENT_DURATION);

  // Get end CPU time of each thread ...
  nanoseconds thread_end = get_thread_time(exec_thread);

  // ... and stop the experiment.
  rclcpp::shutdown();
  exec_thread.join();

  ping_node->print_statistics(EXPERIMENT_DURATION);

  // Print CPU times.
  int64_t high_prio_thread_duration_ms = std::chrono::duration_cast<milliseconds>(
    thread_end - thread_begin).count();
  RCLCPP_INFO(
    logger, "Executor thread ran for %" PRId64 "ms.", high_prio_thread_duration_ms);
  return 0;
}