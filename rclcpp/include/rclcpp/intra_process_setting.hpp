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


// this is needed for DPS middleware that does not allow to count subscriptions
#define COMM_TYPE_INTRA_ONLY 1
#define COMM_TYPE_INTER_ONLY 2
#define COMM_TYPE_INTRA_INTER 3

/**
 * SELECT THE TYPE OF COMMUNICATION USED BY YOUR APP
 */

#define COMM_TYPE COMM_TYPE_INTRA_ONLY


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
