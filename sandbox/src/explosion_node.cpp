#include "explosion_node.hpp"

ExplosionNode::ExplosionNode(std::string name, int start, int end, int factor) : rclcpp::Node(name) {
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
        int pub_start = (i - start) * factor;

        subs.push_back(
            this->create_subscription<std_msgs::msg::Int32>(get_topic_name(i), 10,
                [this, pub_start, factor, i](std_msgs::msg::Int32::ConstSharedPtr msg) {
                    this->sub_callback(msg, pub_start, pub_start + factor, i);
                })
            );
    }
}

std::string ExplosionNode::get_topic_name(int i) {
    char buffer[16];
    sprintf(buffer, "topic_%d", i);
    return std::string(buffer);
}

void ExplosionNode::sub_callback(std_msgs::msg::Int32::ConstSharedPtr msg, int start, int end, int id) {
    //printf("Publishing topics %d to %d, %d available\n", start, end, pubs.size());
    std_msgs::msg::Int32 msg_out;
    msg_out.data = msg->data ^ this->now().nanoseconds() ^ id;
    for(int j = start; j < end; j++) {
        pubs[j]->publish(msg_out);
        //printf("Sub %d publishing on topic %s, %d\n", id, pubs[j]->get_topic_name(), j);
    }
}