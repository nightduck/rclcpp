
#include "rclcpp/node.hpp"

namespace rclcpp
{
namespace realtime
{
// Contains parameters for implementing arbitrary fixed-priority job-level non-preemptive scheduling
class Task
{
public:
  using SharedPtr = std::shared_ptr<Task>;

  int period;
  int deadline;
  int wcet;
  
  std::function<int()> callback_job_deadline;

  uint64_t thread_mask;

  std::vector<Task::SharedPtr> children;
  std::vector<Task::SharedPtr> parents;
};

// Forward declaration of PublisherRT
template<typename MessageT, typename AllocatorT = std::allocator<void>>
class Publisher : public rclcpp::Publisher<MessageT,AllocatorT>, public Task
{
  Publisher(const rclcpp::Publisher<MessageT,AllocatorT> & publisher, const Task & rt_info)
    : rclcpp::Publisher<MessageT, AllocatorT>(publisher),
      Task(rt_info)
  {
    // Additional copy constructor logic, if needed
  }
};

// TODO: Support rest of Subscription template parameters: SubscribedT, ROSMessageT, MessageMemoryStrategyT
// Forward declaration of SubscriptionRT
template<
  typename MessageT,
  typename AllocatorT = std::allocator<void>
  >
class Subscription : public rclcpp::Subscription<MessageT,AllocatorT>, public Task
{
  Subscription(const rclcpp::Subscription<MessageT,AllocatorT> & subscription, const Task & rt_info)
    : rclcpp::Subscription<MessageT,AllocatorT>(subscription),
      Task(rt_info)
  {
    // Additional copy constructor logic, if needed
  }
};

// Forward declaration of ServiceRT
template<typename ServiceT>
class Service : public rclcpp::Service<ServiceT>, public Task
{
  Service(const rclcpp::Service<ServiceT> & service, const Task & rt_info)
    : rclcpp::Service<ServiceT>(service),
      Task(rt_info)
  {
    // Additional copy constructor logic, if needed
  }
};

// Forward declaration of ClientRT
template<typename ServiceT>
class Client : public rclcpp::Client<ServiceT>, public Task
{
  Client(const rclcpp::Client<ServiceT> & client, const Task & rt_info)
    : rclcpp::Client<ServiceT>(client),
      Task(rt_info)
  {
    // Additional copy constructor logic, if needed
  }
};

// Forward declaration of TimerRT
template<typename DurationRepT = int64_t, typename DurationT = std::milli, typename CallbackT>
class Timer : public rclcpp::Timer<DurationRepT, DurationT, CallbackT>, public Task
{
  Timer(const rclcpp::Timer<DurationRepT, DurationT, CallbackT> & timer, const Task & rt_info)
    : rclcpp::Timer<DurationRepT, DurationT, CallbackT>(timer),
      Task(rt_info)
  {
    // Additional copy constructor logic, if needed
  }
};

class Node : public rclcpp::Node
{
public:

  template<
    typename DurationRepT = int64_t,
    typename DurationT = std::milli,
    typename CallbackT,
    typename TimerRT = rclcpp::realtime::Timer<DurationRepT, DurationT, CallbackT>>
  std::shared_ptr<TimerRT>
  create_timer(
    const std::chrono::duration<DurationRepT, DurationT> & period,
    CallbackT callback,
    const Task & rt_info,
    std::initializer_list<rclcpp::PublisherBase::SharedPtr> publishers = {},
    rclcpp::CallbackGroup::SharedPtr group = nullptr
    )
  {
    return std::make_shaked<TimerRT>(
      create_timer<DurationRepT, DurationT, CallbackT>(period, callback, group, publishers),
      rt_info
    );
  }

  template<
    typename MessageT,
    typename AllocatorT = std::allocator<void>,
    typename PublisherRT = rclcpp::realtime::Publisher<MessageT, AllocatorT>>
  std::shared_ptr<PublisherRT>
  create_publisher(
    const std::string & topic_name,
    size_t qos_depth,
    const rclcpp::QoS & qos,
    const rclcpp::PublisherOptionsWithAllocator<AllocatorT> & options,
    const Task & rt_info)
  {
    return std::make_shaked<PublisherRT>(
      create_publisher<MessageT, AllocatorT>(topic_name, qos_depth, qos, options),
      rt_info
    );
  }

  template<
    typename MessageT,
    typename AllocatorT = std::allocator<void>,
    typename SubscriptionRT = rclcpp::realtime::Subscription<MessageT, AllocatorT>>
  std::shared_ptr<SubscriptionRT>
  create_subscription(
    const std::string & topic_name,
    size_t qos_depth,
    const rclcpp::QoS & qos,
    const rclcpp::SubscriptionOptionsWithAllocator<AllocatorT> & options,
    const Task & rt_info)
  {
    return std::make_shaked<SubscriptionT>(
      create_subscription<MessageT, AllocatorT>(topic_name, qos_depth, qos, options),
      rt_info
    );
  }

  template<typename ServiceT, typename ServiceRT = rclcpp::realtime::Service<ServiceT>>
  std::shared_ptr<ServiceT>
  create_service(
    const std::string & service_name,
    std::function<void(std::shared_ptr<typename ServiceT::Request>, std::shared_ptr<typename ServiceT::Response>)> callback,
    const Task & rt_info,
    const rclcpp::QoS & qos = rclcpp::ServicesQoS(),
    rclcpp::CallbackGroup::SharedPtr group = nullptr)
  {
    return std::make_shaked<ServiceT>(
      create_service<ServiceT>(service_name, callback, qos, options),
      rt_info
    );
  }

  template<typename ServiceT, typename ClientRT = rclcpp::realtime::Client<ServiceT>>
  std::shared_ptr<ClientRT>
  create_client(
    const std::string & service_name,
    const rclcpp::QoS & qos = rclcpp::ServicesQoS(),
    const Task & rt_info,
    rclcpp::CallbackGroup::SharedPtr group = nullptr)
  {
    return std::make_shaked<ClientT>(
      create_client<ServiceT>(service_name, qos, options),
      rt_info
    );
  }

// Hide the original create_publisher and create_subscription functions
private:
  using rclcpp::Node::create_timer;
  using rclcpp::Node::create_publisher;
  using rclcpp::Node::create_subscription;
  using rclcpp::Node::create_service;
  using rclcpp::Node::create_client;

};  // class Node

}   // namespace realtime

}   // namespace rclcpp
