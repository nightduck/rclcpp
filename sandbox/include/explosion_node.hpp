#include <functional>
#include <vector>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

using std::placeholders::_1;

class ExplosionNode : public rclcpp::Node {
public:
    /**
     * @brief Construct a new Explosion Node object
     * 
     * @param name Name of node
     * @param start Beginning of topic range that this node subscribes to (inclusive)
     * @param end End of topic range that this node subscribes to (not inclusive)
     * @param factor Number of topics each sub in this node publishes to
     */
    ExplosionNode(std::string name, int start, int end, int factor);

    std::string get_topic_name(int i);

    void sub_callback(std_msgs::msg::Int32::ConstSharedPtr msg, int start, int end, int id);

    std::vector<std::shared_ptr<rclcpp::Subscription<std_msgs::msg::Int32>>> subs;
    std::vector<std::shared_ptr<rclcpp::Publisher<std_msgs::msg::Int32>>> pubs;
};