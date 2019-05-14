// Copyright 2019 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__INTRA_PROCESS_SETTING_HPP_
#define RCLCPP__INTRA_PROCESS_SETTING_HPP_


/**
 * Preprocessor directives for selecting IPC implementation
 */

#define QUEUE_TYPE_NO_QUEUE 0
#define QUEUE_TYPE_SIMPLE 1 // "rclcpp/queues/cpqueue.hpp"
#define QUEUE_TYPE_CONCURRENT 2 // "rclcpp/queues/concurrentqueue.h"
#define QUEUE_TYPE_BLOCKING 3 // "rclcpp/queues/blockingconcurrentqueue.h"
#define QUEUE_TYPE QUEUE_TYPE_SIMPLE

#define IPC_TYPE_DEFAULT 1
#define IPC_TYPE_QUEUE_THREAD 2
#define IPC_TYPE_QUEUE_SPIN 3
#define IPC_TYPE_DIRECT_DISPATCH 4
#define IPC_TYPE IPC_TYPE_DIRECT_DISPATCH

// disable the queues if not needed
#if IPC_TYPE == IPC_TYPE_DEFAULT || IPC_TYPE == IPC_TYPE_DIRECT_DISPATCH
#undef QUEUE_TYPE
#define QUEUE_TYPE QUEUE_TYPE_NO_QUEUE
#endif

// ensure that a queue is present if the IPC type requires it
#if (IPC_TYPE == IPC_TYPE_QUEUE_THREAD || IPC_TYPE == IPC_TYPE_QUEUE_SPIN) && QUEUE_TYPE == QUEUE_TYPE_NO_QUEUE
#error "The IPC type requires a queue, but it is set to NO_QUEUE"
#endif


namespace rclcpp
{

/// Used as argument in create_publisher and create_subscriber.
enum class IntraProcessSetting
{
  /// Explicitly enable intraprocess comm at publisher/subscription level.
  Enable,
  /// Explicitly disable intraprocess comm at publisher/subscription level.
  Disable,
  /// Take intraprocess configuration from the node.
  NodeDefault
};

}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_SETTING_HPP_
