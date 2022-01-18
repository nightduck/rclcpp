#include <cinttypes>
#include <cstdlib>
#include <ctime>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"
#include "rclcpp/executor.hpp"
#include "rclcpp/executors/fixed_prio_executor.hpp"
#include "explosion_node.hpp"

#include "sandbox/utilities.hpp"

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

using sandbox::configure_thread;
using sandbox::get_thread_time;

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

    auto pp = [](rclcpp::AnyExecutable exec) -> int {
        int priority = 50;
        // if (exec.subscription != NULL) {
        //     priority = 1e9 / exec.subscription->get_actual_qos().deadline().nanoseconds();
        // } else if (exec.timer != NULL) {
        //     int64_t period = 0;
        //     rcl_timer_get_period(exec.timer->get_timer_handle().get(), &period);
        //     priority = 1e9 / period;
        // }
        priority = std::min(std::max(priority,1),99);
        return priority;
    };

    auto tmr_node = std::make_shared<rclcpp::Node>("tmr");

    auto pub1 = tmr_node->create_publisher<std_msgs::msg::Int32>("topic_2", 10);
    auto pub2 = tmr_node->create_publisher<std_msgs::msg::Int32>("topic_3", 10);
    auto tmr1 = tmr_node->create_wall_timer(1000ms, [&](void){
        std_msgs::msg::Int32 msg;
        msg.data = tmr_node->now().nanoseconds();
        pub1->publish(msg);
        msg.data = tmr_node->now().nanoseconds();
        pub2->publish(msg);
    });
    
    rclcpp::Logger logger = tmr_node->get_logger();

    auto qos = rclcpp::QoS(50);
    qos.deadline(rclcpp::Duration(50ms));

    // Create a bunch of explosion nodes
    std::list<std::shared_ptr<ExplosionNode>> nodes;
    for (int i = 2; i < 0x800; i = i << 1) {
        char buffer[16];
        sprintf(buffer, "node_%d", i);
        nodes.push_back(
            std::make_shared<ExplosionNode>(std::string(buffer), i, i << 1, 2)
        );
    }

    const std::chrono::seconds EXPERIMENT_DURATION = 20s;

    std::this_thread::sleep_for(3s);
    
    if (argc >= 2 && std::string("fp").compare(argv[1]) == 0) {
        rclcpp::executors::FixedPrioExecutor exec(pp);

        exec.add_node(tmr_node);

        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.add_node(n);
        }

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        RCLCPP_INFO_STREAM(
            logger, "Running new executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr_node);
        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.remove_node(n);
        }
    } else if (argc >= 2 && std::string("st").compare(argv[1]) == 0) {
        rclcpp::executors::SingleThreadedExecutor exec;

        // TODO: Simulate PiCAS with multiple ST executors, many with multiple nodes

        exec.add_node(tmr_node);

        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.add_node(n);
        }

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        RCLCPP_INFO_STREAM(
            logger, "Running new executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr_node);
        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.remove_node(n);
        }
    } else if (argc >= 2 && std::string("sst").compare(argv[1]) == 0) {
        rclcpp::executors::StaticSingleThreadedExecutor exec;

        // TODO: Simulate PiCAS with multiple ST executors, many with multiple nodes

        exec.add_node(tmr_node);

        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.add_node(n);
        }

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        RCLCPP_INFO_STREAM(
            logger, "Running new executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr_node);
        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.remove_node(n);
        }
    } else {
        rclcpp::executors::MultiThreadedExecutor exec(rclcpp::ExecutorOptions(), 4);

        exec.add_node(tmr_node);

        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.add_node(n);
        }

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        RCLCPP_INFO_STREAM(
            logger, "Running new executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr_node);
        for(std::shared_ptr<ExplosionNode> n : nodes) {
            exec.remove_node(n);
        }
    }
}