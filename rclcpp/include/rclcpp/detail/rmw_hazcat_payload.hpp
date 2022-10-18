// Copyright 2022 Washington University in St Louis
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

#ifndef RCLCPP__DETAIL__RMW_HAZCAT_PAYLOAD_HPP_
#define RCLCPP__DETAIL__RMW_HAZCAT_PAYLOAD_HPP_

#include "hazcat_allocators/hma_template.h"
#include "rclcpp/detail/rmw_implementation_specific_publisher_payload.hpp"
#include "rclcpp/detail/rmw_implementation_specific_subscription_payload.hpp"

namespace rclcpp
{
namespace detail
{

using Payload = rclcpp::detail::RMWImplementationSpecificPayload;
using PublisherLoad = rclcpp::detail::RMWImplementationSpecificPublisherPayload;
using SubscriptionLoad = rclcpp::detail::RMWImplementationSpecificSubscriptionPayload;

class RCLCPP_PUBLIC HazcatPayload
  : protected PublisherLoad,
    protected SubscriptionLoad
{
public:
  // TODO: Add constructor that is wrapper for allocator creation
  HazcatPayload(hma_allocator_t * alloc) {
    this->alloc = alloc;
  }

  const char * get_implementation_identifier() const {
    return rmw_get_implementation_identifier();
  }

  bool
  has_been_customized() const {
    return true;
  }

  void
  modify_rmw_publisher_options(rmw_publisher_options_t & rmw_publisher_options) const {
    rmw_publisher_options.rmw_specific_publisher_payload = alloc;
  }

  void
  modify_rmw_subscription_options(rmw_subscription_options_t & rmw_subscription_options) const {
    rmw_subscription_options.rmw_specific_subscription_payload = alloc;
  }

  hma_allocator_t * alloc;
};

}  // namespace detail
}  // namespace rclcpp

#endif  // RCLCPP__DETAIL__RMW_HAZCAT_PAYLOAD_HPP_