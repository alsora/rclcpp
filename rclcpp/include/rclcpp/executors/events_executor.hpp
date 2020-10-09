// Copyright 2020 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__EXECUTORS__EVENTS_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__EVENTS_EXECUTOR_HPP_

#include <queue>
#include <chrono>

#include "rclcpp/executor.hpp"
#include "rclcpp/executors/events_executor_entities_collector.hpp"
#include "rclcpp/executors/timers_manager.hpp"
#include "rclcpp/node.hpp"

#include "rcutils/event_types.h"

namespace rclcpp
{
namespace executors
{

/// Events executor implementation
/**
 * Add description
 *
 * To run this executor:
 * rclcpp::executors::EventsExecutor executor;
 * executor.add_node(node);
 * executor.spin();
 * executor.remove_node(node);
 */
class EventsExecutor : public rclcpp::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(EventsExecutor)

  /// Default constructor. See the default constructor for Executor.
  RCLCPP_PUBLIC
  explicit EventsExecutor(
    const rclcpp::ExecutorOptions & options = rclcpp::ExecutorOptions());

  /// Default destrcutor.
  RCLCPP_PUBLIC
  virtual ~EventsExecutor();

  /// Events executor implementation of spin.
  /**
   * This function will block until work comes in, execute it, and keep blocking.
   * It will only be interrupted by a CTRL-C (managed by the global signal handler).
   * \throws std::runtime_error when spin() called while already spinning
   */
  RCLCPP_PUBLIC
  void
  spin() override;

  /// Events executor implementation of spin some
  /**
   * executor.provide_callbacks();
   * while(condition) {
   *   executor.spin_some();
   * }
   */
  RCLCPP_PUBLIC
  void
  spin_some(std::chrono::nanoseconds max_duration) override;

  /// Add a node to the executor.
  /**
   * \sa rclcpp::Executor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::EventsExecutor::add_node
   */
  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  /// Remove a node from the executor.
  /**
   * \sa rclcpp::Executor::remove_node
   */
  RCLCPP_PUBLIC
  void
  remove_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  /**
   * \sa rclcpp::Executor::remove_node
   */
  RCLCPP_PUBLIC
  void
  remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

protected:
  /// Execute timers when ready
  RCLCPP_PUBLIC
  void
  spin_timers(bool spin_once);

  /// Execute events in the queue until is empty
  RCLCPP_PUBLIC
  void
  execute_events();

private:
  RCLCPP_DISABLE_COPY(EventsExecutor)

  EventsExecutorEntitiesCollector::SharedPtr entities_collector_;

  // Executor callback: Push new events into the queue and trigger cv.
  // This function is called by the DDS entities when an event happened,
  // like a subscription receiving a message.
  static void
  push_event(const void * executor_ptr, EventQ event)
  {
    // Cast executor_ptr to this (need to remove constness)
    auto this_executor = const_cast<executors::EventsExecutor*>(
                  static_cast<const executors::EventsExecutor*>(executor_ptr));

    // Event queue mutex scope
    {
      std::unique_lock<std::mutex> lock(this_executor->event_queue_mutex_);

      this_executor->event_queue.push(event);
    }
    // Notify that the event queue has some events in it.
    this_executor->event_queue_cv.notify_one();
  }

  // Event queue members
  std::queue<EventQ> event_queue;
  std::mutex event_queue_mutex_;
  std::condition_variable event_queue_cv;

  // Timers heap manager
  TimersManager timers;
};

}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__EVENTS_EXECUTOR_HPP_
