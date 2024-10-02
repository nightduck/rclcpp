// THIS FILE SHOULD NOT BE INCLUDED IN ANY PULl REQUESTS
// Temp file to control what features are enable in the various branches I maintain. This allows me
// to prevent build errors in downstream depedencies as include files are added/removed

#ifndef RCLCPP__EXPERIMENTAL__EXPERIMENTAL_DEFINITION_HPP_
#define RCLCPP__EXPERIMENTAL__EXPERIMENTAL_DEFINITION_HPP_

#define RCLCPP_EXPERIMENTAL_PRIORITY_QUEUE_ENABLED 1
// This is the two-part queue that doesn't require graph awareness
#define RCLCPP_EXPERIMENTAL_PERIOD_QUEUE_ENABLED 1      
#define RCLCPP_EXPERIMENTAL_RM_QUEUE_ENABLED 0
#define RCLCPP_EXPERIMENTAL_EDF_QUEUE_ENABLED 0
#define RCLCPP_EXPERIMENTAL_GRAPH_EXECUTOR_ENABLED 0
#define RCLCPP_EXPERIMENTAL_MT_EVENTS_EXECUTOR_ENABLED 0

#endif  // RCLCPP__EXPERIMENTAL__EXPERIMENTAL_DEFINITION_HPP_
