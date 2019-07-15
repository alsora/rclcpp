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

#ifndef RCLCPP__SUBSCRIPTION_INTRA_PROCESS_HPP_
#define RCLCPP__SUBSCRIPTION_INTRA_PROCESS_HPP_

#include <rmw/rmw.h>

#include <functional>
#include <memory>
#include <utility>

#include "rcl/error_handling.h"

#include "rclcpp/contexts/default_context.hpp"
#include "rclcpp/buffers/intra_process_buffer.hpp"
#include "rclcpp/subscription.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/waitable.hpp"

namespace rclcpp
{

// Forward declarations
namespace node_interfaces
{
class NodeTopicsInterface;
class NodeWaitablesInterface;
}  // namespace node_interfaces


class SubscriptionIntraProcessBase : public rclcpp::Waitable
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SubscriptionIntraProcessBase)

  SubscriptionIntraProcessBase() {}

  size_t
  get_number_of_ready_guard_conditions() {return 1;}

  virtual bool
  add_to_wait_set(rcl_wait_set_t * wait_set) = 0;

  virtual bool
  is_ready(rcl_wait_set_t * wait_set) = 0;

  virtual void
  execute() = 0;

  virtual bool
  use_take_shared_method() const = 0;

  virtual const char *
  get_topic_name() const = 0;

  virtual rmw_qos_profile_t
  get_actual_qos() const = 0;

private:
  virtual void
  trigger_guard_condition() = 0;
};


template<
  typename MessageT,
  typename Alloc = std::allocator<void>>
class SubscriptionIntraProcess : public SubscriptionIntraProcessBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SubscriptionIntraProcess)

  using MessageAllocTraits = allocator::AllocRebind<MessageT, Alloc>;
  using MessageAlloc = typename MessageAllocTraits::allocator_type;
  using MessageDeleter = allocator::Deleter<MessageAlloc, MessageT>;
  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT, MessageDeleter>;

  using BufferSharedPtr = typename intra_process_buffer::IntraProcessBuffer<MessageT>::SharedPtr;

  SubscriptionIntraProcess(
    AnySubscriptionCallback<MessageT, Alloc> callback,
    const std::string & topic_name,
    rmw_qos_profile_t qos_profile,
    BufferSharedPtr buffer)
  : any_callback_(callback), buffer_(buffer), topic_name_(topic_name), qos_profile_(qos_profile)
  {
    std::shared_ptr<rclcpp::Context> context_ptr =
      rclcpp::contexts::default_context::get_global_default_context();

    rcl_guard_condition_options_t guard_condition_options =
      rcl_guard_condition_get_default_options();

    gc_ = rcl_get_zero_initialized_guard_condition();
    rcl_ret_t ret = rcl_guard_condition_init(
      &gc_, context_ptr->get_rcl_context().get(), guard_condition_options);

    if (RCL_RET_OK != ret) {
      throw std::runtime_error("SubscriptionIntraProcess init error initializing guard condition");
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
    if (any_callback_.use_take_shared_method()) {
      ConstMessageSharedPtr msg = buffer_->consume_shared();
      any_callback_.dispatch_intra_process(msg, rmw_message_info_t());
    } else {
      MessageUniquePtr msg = buffer_->consume_unique();
      any_callback_.dispatch_intra_process(std::move(msg), rmw_message_info_t());
    }
  }

  void
  provide_intra_process_message(ConstMessageSharedPtr message)
  {
    buffer_->add_shared(message);
    trigger_guard_condition();
  }

  void
  provide_intra_process_message(MessageUniquePtr message)
  {
    buffer_->add_unique(std::move(message));
    trigger_guard_condition();
  }

  bool
  use_take_shared_method() const
  {
    return buffer_->use_take_shared_method();
  }

  const char *
  get_topic_name() const
  {
    return topic_name_.c_str();
  }

  rmw_qos_profile_t
  get_actual_qos() const
  {
    return qos_profile_;
  }

private:
  void
  trigger_guard_condition()
  {
    rcl_ret_t ret = rcl_trigger_guard_condition(&gc_);
    (void)ret;
  }

  std::recursive_mutex reentrant_mutex_;
  rcl_guard_condition_t gc_;

  AnySubscriptionCallback<MessageT, Alloc> any_callback_;
  BufferSharedPtr buffer_;

  std::string topic_name_;
  rmw_qos_profile_t qos_profile_;
};

}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_INTRA_PROCESS_HPP_
