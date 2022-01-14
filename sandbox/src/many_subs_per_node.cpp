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

using std::placeholders::_1;

class ExplosionNode : rclcpp::Node {
public:

    std::string get_topic_name(int i) {
        char buffer[16];
        sprintf(buffer, "topic_%d", i);
        return std::string(buffer);
    }

    void sub_callback(std_msgs::msg::Int32::ConstSharedPtr msg, int start, int end){
        std_msgs::msg::Int32 msg_out;
        msg_out.data = msg->data ^ this->now().nanoseconds();
        for(int j = start; j < end; j++) {
            pubs[j]->publish(msg_out);
        }
    }
    /**
     * @brief Construct a new Explosion Node object
     * 
     * @param name Name of node
     * @param start Beginning of topic range that this node subscribes to (inclusive)
     * @param end End of topic range that this node subscribes to (not inclusive)
     * @param factor Number of topics each sub in this node publishes to
     */
    ExplosionNode(std::string name, int start, int end, int factor) : rclcpp::Node(name) {
        // TODO: Redefine factor to be like kernel-size and step in conv layer. So subs can publish to the same topics, with some overlap
        assert(end > start);
        subs.reserve(end - start);
        pubs.reserve((end - start) * factor);
        int range = end - start;
        
        // Create all publishers, starting at index "end", and "factor" times more than subs
        for(int i = end; i < factor * range + end; i++) {
            pubs.push_back(this->create_publisher<std_msgs::msg::Int32>(get_topic_name(i), 10));
        }

        // Create all subscribers, starting at "start" and ending before "end", each publishing to
        // "factor" topics
        for (int i = start; i < end; i++) {
            int pub_start = end + (i - start) * factor;

            subs.push_back(
                this->create_subscription<std_msgs::msg::Int32>(get_topic_name(i), 10,
                    [this, pub_start, factor](std_msgs::msg::Int32::ConstSharedPtr msg) {
                        this->sub_callback(msg, pub_start, pub_start * factor);
                    })
                );
        }
    }

    std::vector<std::shared_ptr<rclcpp::Subscription<std_msgs::msg::Int32>>> subs;
    std::vector<std::shared_ptr<rclcpp::Publisher<std_msgs::msg::Int32>>> pubs;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

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

    auto tmr_node = std::make_shared<rclcpp::Node>("tmr");
    
    rclcpp::Logger logger = tmr_node->get_logger();

    auto qos = rclcpp::QoS(50);
    qos.deadline(rclcpp::Duration(50ms));

    // TODO: Create a bunch of explosion nodes here


    const std::chrono::seconds EXPERIMENT_DURATION = 10s;
    
    if (argc >= 2 && std::string("fp").compare(argv[1]) == 0) {
        rclcpp::executors::FixedPrioExecutor exec(pp);

        exec.add_node(tmr_node);

        // TODO: Add all explosion nodes

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
        // TODO: Remove all explosion nodes
    } else if (argc >= 2 && std::string("st").compare(argv[1]) == 0) {
        rclcpp::executors::SingleThreadedExecutor exec;

        // TODO: Simulate PiCAS with multiple ST executors, many with multiple nodes

        exec.add_node(tmr_node);

        // TODO: Add all explosion nodes

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
        // TODO: Remove all explosion nodes
    } else if (argc >= 2 && std::string("sst").compare(argv[1]) == 0) {
        rclcpp::executors::StaticSingleThreadedExecutor exec;

        // TODO: Simulate PiCAS with multiple ST executors, many with multiple nodes

        exec.add_node(tmr_node);

        // TODO: Add all explosion nodes

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
        // TODO: Remove all explosion nodes
    } else {
        rclcpp::executors::MultiThreadedExecutor exec;

        exec.add_node(tmr_node);

        // TODO: Add all explosion nodes

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
        // TODO: Remove all explosion nodes
    }
}