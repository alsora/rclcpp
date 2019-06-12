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

#ifndef RCLCPP__SUBSCRIPTION_INTRA_PROCESS_WAITABLE_HPP_
#define RCLCPP__SUBSCRIPTION_INTRA_PROCESS_WAITABLE_HPP_

#include <rmw/error_handling.h>
#include <rmw/rmw.h>

#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>


#include "rcl/error_handling.h"
#include "rcl/subscription.h"

#include "rcl_interfaces/msg/intra_process_message.hpp"

#include "rclcpp/any_subscription_callback.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/contexts/default_context.hpp"
#include "rclcpp/exceptions.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/message_memory_strategy.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/subscription_traits.hpp"
#include "rclcpp/time.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/waitable.hpp"

namespace rclcpp
{

namespace node_interfaces
{
class NodeTopicsInterface;
class NodeWaitablesInterface;
}  // namespace node_interfaces


class IntraProcessSubscriptionWaitableBase : public rclcpp::Waitable
{
public:
  IntraProcessSubscriptionWaitableBase() {}

  size_t
  get_number_of_ready_guard_conditions() {return 1;}

  virtual bool
  add_to_wait_set(rcl_wait_set_t * wait_set) = 0;

  virtual bool
  is_ready(rcl_wait_set_t * wait_set) = 0;

  virtual void
  execute() = 0;

  void
  trigger_guard_condition()
  {
    rcl_trigger_guard_condition(&gc_);
  }


  rcl_guard_condition_t gc_;
};

class IntraProcessSubscriptionWaitable : public IntraProcessSubscriptionWaitableBase
{
public:

  std::recursive_mutex reentrant_mutex_;
  SubscriptionBase::WeakPtr sub_;
  std::shared_ptr<intra_process_buffer::IntraProcessBufferBase> buffer_;

  IntraProcessSubscriptionWaitable(
    SubscriptionBase::WeakPtr subscription,
    std::shared_ptr<intra_process_buffer::IntraProcessBufferBase> buffer)
  : sub_(subscription), buffer_(buffer)
  {
    std::shared_ptr<rclcpp::Context> context_ptr =
      rclcpp::contexts::default_context::get_global_default_context();

    rcl_guard_condition_options_t guard_condition_options =
      rcl_guard_condition_get_default_options();

    gc_ = rcl_get_zero_initialized_guard_condition();
    rcl_ret_t ret = rcl_guard_condition_init(
      &gc_, context_ptr->get_rcl_context().get(), guard_condition_options);

    if (RCL_RET_OK != ret) {
      throw std::runtime_error("IntraProcessWaitable init error initializing guard condition");
    }
  }

  bool
  add_to_wait_set(rcl_wait_set_t * wait_set)
  {
    std::lock_guard<std::recursive_mutex> lock(reentrant_mutex_);

    rcl_ret_t ret = rcl_wait_set_add_guard_condition(wait_set, &gc_, NULL);
    return RCL_RET_OK == ret;
  }

  bool
  is_ready(rcl_wait_set_t * wait_set)
  {
    (void)wait_set;
    return buffer_->has_data();
  }

  void execute()
  {
    auto subscription = sub_.lock();
    if (!subscription) {
      throw std::runtime_error("subscription has unexpectedly gone out of scope");
    }

    subscription->execute_intra_process_subscription();
  }
};
}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_INTRA_PROCESS_WAITABLE_HPP_
