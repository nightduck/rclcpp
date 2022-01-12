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
#include <list>
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

std::atomic_ulong callback_time;

void do_work(std::string s) {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << s << std::endl;
    auto end = std::chrono::high_resolution_clock::now();
    callback_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
}

void burn_cycles_and_publish(
        std::vector<std::shared_ptr<rclcpp::Publisher<std_msgs::msg::Int32>>> pubs,
        int pub_start, int pub_end, std::chrono::milliseconds burn_time, int seed, int id) {
    auto start = std::chrono::high_resolution_clock::now();     
    
    auto now = std::chrono::high_resolution_clock::now();
    // if (id == -1)
    //     std::cout << "Timer 1" << std::endl;
    // else if (id == -2)
    //     std::cout << "Timer 2" << std::endl;
    // else
    //     std::cout << "Topic " << id << std::endl;

    while(std::chrono::high_resolution_clock::now() - now < burn_time) {
        seed = seed * seed * seed;
    }

    std_msgs::msg::Int32 msg;
    msg.data = seed;
    for (int i = pub_start; i < pub_end; i++) {
        msg.data &= 0xFFFFFF00;
        msg.data |= i;                  // First 24 bits are random computation. Last 8 are topic #
        //std::cout << "publishing " << msg.data << " to " << pubs[i]->get_topic_name() << std::endl;
        pubs[i]->publish(msg);
    }

    auto end = std::chrono::high_resolution_clock::now();
    callback_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
}

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

    std::atomic_init(&callback_time, 0);

    auto pp = [](rclcpp::AnyExecutable exec) -> int {
        int priority = 50;
        if (exec.subscription != NULL) {
            auto name = exec.subscription->get_topic_name();
            priority = 60+std::atoi(name+6);    // Get priority from topic name
        } else if (exec.timer != NULL) {
            int64_t period = 0;
            rcl_timer_get_period(exec.timer->get_timer_handle().get(), &period);
            if (period > 2000000000) {           // Slower timer is lower priority
                priority = 50;
            } else {
                priority = 90;
            }
        }
        priority = std::min(std::max(priority,1),99);
        return priority;
    };

    auto tmr_node = std::make_shared<rclcpp::Node>("tmr");
    auto sub1_node = std::make_shared<rclcpp::Node>("sub1");
    auto sub2_node = std::make_shared<rclcpp::Node>("sub2");
    
    rclcpp::Logger logger = tmr_node->get_logger();

    auto qos = rclcpp::QoS(50);

    qos.deadline(rclcpp::Duration(400ms));
    std::vector<std::shared_ptr<rclcpp::Publisher<std_msgs::msg::Int32>>> publishers;
    std::vector<std::shared_ptr<rclcpp::Subscription<std_msgs::msg::Int32>>> subscribers;
    publishers.reserve(16);
    subscribers.reserve(16);

    auto tmr1 = tmr_node->create_wall_timer(1200ms, [&](void){
        burn_cycles_and_publish(publishers, 0, 8, 100ms, 0xDEADBEEF, -1);
    });
    auto tmr2 = tmr_node->create_wall_timer(2400ms, [&](void){
        burn_cycles_and_publish(publishers, 0, 8, 100ms, 0xDEADBEEF, -2);
    });

    int ints[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    for(int i = 0; i < 8; i++) {
        char char_buff[16];
        snprintf(char_buff, sizeof(char_buff), "topic_%d", i);
        std::string topic_name = char_buff;
        publishers.push_back(tmr_node ->create_publisher<std_msgs::msg::Int32>(
            topic_name, qos));
        subscribers.push_back(sub1_node->create_subscription<std_msgs::msg::Int32>(
            topic_name, qos, [&, i](std_msgs::msg::Int32::ConstSharedPtr msg) {
                burn_cycles_and_publish(publishers, msg->data & 0xFF, 16, 80ms, msg->data, i);
            }
        ));
    }


    for(int i = 8; i < 16; i++) {
        char char_buff[16];
        snprintf(char_buff, sizeof(char_buff), "topic_%d", i);
        std::string topic_name = char_buff;
        publishers.push_back(sub1_node->create_publisher<std_msgs::msg::Int32>(
            topic_name, qos));
        subscribers.push_back(sub2_node->create_subscription<std_msgs::msg::Int32>(
            topic_name, qos, [&, i](std_msgs::msg::Int32::ConstSharedPtr msg) {
                burn_cycles_and_publish(publishers, 16, 16, 10ms, msg->data, i);
            }
        ));
    }
    
    if (argc >= 2 && std::string("fp").compare(argv[1]) == 0) {
        rclcpp::executors::FixedPrioExecutor exec(pp);

        exec.add_node(tmr_node);
        exec.add_node(sub1_node);
        exec.add_node(sub2_node);

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
        exec.remove_node(tmr_node);
        exec.remove_node(sub1_node);
        exec.remove_node(sub2_node);
    } else {
        rclcpp::executors::MultiThreadedExecutor exec;

        exec.add_node(tmr_node);
        exec.add_node(sub1_node);
        exec.add_node(sub2_node);

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
        exec.remove_node(tmr_node);
        exec.remove_node(sub1_node);
        exec.remove_node(sub2_node);
    }

    std::cout << "Time in callbacks: " << std::atomic_load(&callback_time) / 1000000000.0 << "s" << std::endl;
}