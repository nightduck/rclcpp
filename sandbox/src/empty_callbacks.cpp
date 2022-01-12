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
#include <atomic>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"
#include "rclcpp/executor.hpp"
#include "rclcpp/executors/fixed_prio_executor.hpp"

#include "sandbox/utilities.hpp"

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using namespace std::chrono_literals;

using sandbox::configure_thread;
using sandbox::get_thread_time;

std::atomic_int callback_time;

void do_work(std::string s) {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << s << std::endl;
    auto end = std::chrono::high_resolution_clock::now();
    callback_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
}

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

    std::atomic_init(&callback_time, 0);

    auto pp = [](rclcpp::AnyExecutable exec) -> int {
        int priority = 50;
        if (exec.subscription != NULL) {
            priority = 1e9 / exec.subscription->get_actual_qos().deadline().nanoseconds();
        } else if (exec.timer != NULL) {
            int64_t period = 0;
            rcl_timer_get_period(exec.timer->get_timer_handle().get(), &period);
            priority = 1e9 / period;
        }
        priority = std::min(std::max(priority,1),99);
        return priority;
    };

    auto tmr1_node = std::make_shared<rclcpp::Node>("tmr1");
    auto tmr2_node = std::make_shared<rclcpp::Node>("tmr2");
    auto sub1_node = std::make_shared<rclcpp::Node>("sub1");
    auto sub2_node = std::make_shared<rclcpp::Node>("sub2");
    auto sub3_node = std::make_shared<rclcpp::Node>("sub3");
    auto sub4_node = std::make_shared<rclcpp::Node>("sub4");
    
    rclcpp::Logger logger = tmr1_node->get_logger();

    auto qos = rclcpp::QoS(50);

    qos.deadline(rclcpp::Duration(50ms));
    auto pub1 = tmr1_node->create_publisher<std_msgs::msg::Int32>("topic_a", qos);
    auto tmr1 = tmr1_node->create_wall_timer(50ms, [&](void){
        do_work("Timer 1");
        std_msgs::msg::Int32 msg;
        msg.data = tmr1_node->now().nanoseconds();
        pub1->publish(msg);
    });
    auto sub1 = sub1_node->create_subscription<std_msgs::msg::Int32>("topic_a", qos,
        [&](std_msgs::msg::Int32::ConstSharedPtr msg){
            do_work("Sub 1");
        });
    auto sub2 = sub2_node->create_subscription<std_msgs::msg::Int32>("topic_a", qos,
        [&](std_msgs::msg::Int32::ConstSharedPtr msg){
            do_work("Sub 2");
        });


    qos.deadline(rclcpp::Duration(100ms));
    auto pub2 = tmr2_node->create_publisher<std_msgs::msg::Int32>("topic_b", qos);
    auto tmr2 = tmr2_node->create_wall_timer(100ms, [&](void){
        do_work("Timer 2");
        std_msgs::msg::Int32 msg;
        msg.data = tmr2_node->now().nanoseconds();
        pub2->publish(msg);
    });
    auto sub3 = sub3_node->create_subscription<std_msgs::msg::Int32>("topic_b", qos,
        [&](std_msgs::msg::Int32::ConstSharedPtr msg){
            do_work("Sub 3");
        });
    auto sub4 = sub4_node->create_subscription<std_msgs::msg::Int32>("topic_b", qos,
        [&](std_msgs::msg::Int32::ConstSharedPtr msg){
            do_work("Sub 4");
        });
    
    if (argc >= 2 && std::string("fp").compare(argv[1]) == 0) {
        rclcpp::executors::FixedPrioExecutor exec(pp);

        exec.add_node(tmr1_node);
        exec.add_node(tmr2_node);
        exec.add_node(sub1_node);
        exec.add_node(sub2_node);
        exec.add_node(sub3_node);
        exec.add_node(sub4_node);

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        const std::chrono::seconds EXPERIMENT_DURATION = 60s;
        RCLCPP_INFO_STREAM(
            logger, "Running new executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr1_node);
        exec.remove_node(tmr2_node);
        exec.remove_node(sub1_node);
        exec.remove_node(sub2_node);
        exec.remove_node(sub3_node);
        exec.remove_node(sub4_node);
    } else {
        rclcpp::executors::MultiThreadedExecutor exec;

        exec.add_node(tmr1_node);
        exec.add_node(tmr2_node);
        exec.add_node(sub1_node);
        exec.add_node(sub2_node);
        exec.add_node(sub3_node);
        exec.add_node(sub4_node);

        // Create a thread for each of the two executors ...
        auto exec_thread = std::thread(
        [&]() {
            exec.spin();
        });

        // Creating the threads immediately started them.
        // Therefore, get start CPU time of each thread now.
        auto thread_begin = get_thread_time(exec_thread);
        const std::chrono::seconds EXPERIMENT_DURATION = 60s;
        RCLCPP_INFO_STREAM(
            logger, "Running default executor from now on for " << EXPERIMENT_DURATION.count() << " seconds ...");
        std::this_thread::sleep_for(EXPERIMENT_DURATION);

        // ... and stop the experiment.
        rclcpp::shutdown();
        exec_thread.join();
        exec.remove_node(tmr1_node);
        exec.remove_node(tmr2_node);
        exec.remove_node(sub1_node);
        exec.remove_node(sub2_node);
        exec.remove_node(sub3_node);
        exec.remove_node(sub4_node);
    }

    std::cout << "Time in callbacks: " << std::atomic_load(&callback_time) << "ns" << std::endl;
}