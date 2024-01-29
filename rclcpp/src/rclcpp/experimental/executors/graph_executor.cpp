// Copyright 2024 Washington University in St Louis
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

#include "rclcpp/experimental/executors/graph_executor.hpp"
#include "rclcpp/experimental/executors/events_executor/priority_events_queue.hpp"

using rclcpp::experimental::executors::GraphExecutor;
using rclcpp::experimental::executors::EventsExecutor;
using rclcpp::experimental::executors::PriorityEventsQueue;

GraphExecutor::GraphExecutor()
: EventsExecutor(std::make_unique<PriorityEventsQueue>())
{
}

GraphExecutor::GraphExecutor(std::function<int(const ExecutorEvent &)> extract_priority)
: EventsExecutor(std::make_unique<PriorityEventsQueue>(extract_priority))
{
}

GraphExecutor::~GraphExecutor()
{
}
