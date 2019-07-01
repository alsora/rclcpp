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

#include "rclcpp/any_subscription_callback.hpp"
#include "rclcpp/buffers/simple_queue_implementation.hpp"
#include "rclcpp/buffers/ring_buffer_implementation.hpp"
#include "rclcpp/contexts/default_context.hpp"
#include "rclcpp/intra_process_buffer.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/waitable.hpp"

namespace rclcpp
{

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

  virtual void
  trigger_guard_condition() = 0;

  virtual std::shared_ptr<intra_process_buffer::IntraProcessBufferBase>
  get_intra_process_buffer() = 0;

  virtual bool
  use_take_shared_method() const = 0;
};


template<
  typename MessageT,
  typename Alloc>
class SubscriptionIntraProcess : public SubscriptionIntraProcessBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(SubscriptionIntraProcess)

  using MessageAllocTraits = allocator::AllocRebind<MessageT, Alloc>;
  using MessageAlloc = typename MessageAllocTraits::allocator_type;
  using MessageDeleter = allocator::Deleter<MessageAlloc, MessageT>;
  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT, MessageDeleter>;

  SubscriptionIntraProcess(
    AnySubscriptionCallback<MessageT, Alloc> * callback_ptr,
    std::shared_ptr<intra_process_buffer::IntraProcessBuffer<MessageT>> buffer)
  : any_callback_(callback_ptr), buffer_(buffer)
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
    if (any_callback_->use_take_shared_method()) {
      ConstMessageSharedPtr msg;
      buffer_->consume(msg);
      any_callback_->dispatch_intra_process(msg, rmw_message_info_t());
    } else {
      MessageUniquePtr msg;
      buffer_->consume(msg);
      any_callback_->dispatch_intra_process(std::move(msg), rmw_message_info_t());
    }
  }

  void
  trigger_guard_condition()
  {
    rcl_ret_t ret = rcl_trigger_guard_condition(&gc_);
    (void)ret;
  }

  std::shared_ptr<intra_process_buffer::IntraProcessBufferBase>
  get_intra_process_buffer()
  {
    return buffer_;
  }

  bool
  use_take_shared_method() const
  {
    return buffer_->use_take_shared_method();
  }

private:
  std::recursive_mutex reentrant_mutex_;
  rcl_guard_condition_t gc_;

  AnySubscriptionCallback<MessageT, Alloc> * any_callback_;
  std::shared_ptr<intra_process_buffer::IntraProcessBuffer<MessageT>> buffer_;
};


template<
  typename MessageT,
  typename Alloc>
std::shared_ptr<SubscriptionIntraProcess<MessageT, Alloc>>
create_subscription_intra_process(
  AnySubscriptionCallback<MessageT, Alloc> * callback,
  IntraProcessBufferType buffer_type,
  const rcl_subscription_options_t & options)
{
  using MessageAllocTraits = allocator::AllocRebind<MessageT, Alloc>;
  using MessageAlloc = typename MessageAllocTraits::allocator_type;
  using MessageDeleter = allocator::Deleter<MessageAlloc, MessageT>;
  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT, MessageDeleter>;

  size_t buffer_size = options.qos.depth;

  if (options.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
    // TODO: this should be the limit of the underlying middleware
    // Also the way in which the memory is allocated in the buffer should be different.
    buffer_size = 1000;
  }

  std::shared_ptr<intra_process_buffer::IntraProcessBuffer<MessageT>> buffer;

    switch (buffer_type) {
      case IntraProcessBufferType::SharedPtr:
        {
          using BufferT = ConstMessageSharedPtr;

          auto buffer_implementation =
            std::make_shared<rclcpp::intra_process_buffer::RingBufferImplementation<BufferT>>(
            buffer_size);

          // construct the intra_process_buffer
          buffer =
            std::make_shared<rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT,
              BufferT>>(buffer_implementation);

          break;
        }
      case IntraProcessBufferType::UniquePtr:
        {
          using BufferT = MessageUniquePtr;

          auto buffer_implementation =
            std::make_shared<rclcpp::intra_process_buffer::RingBufferImplementation<BufferT>>(
            buffer_size);

          // construct the intra_process_buffer
          buffer =
            std::make_shared<rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT,
              BufferT>>(buffer_implementation);

          break;
        }
      case IntraProcessBufferType::MessageT:
        {
          using BufferT = MessageT;

          auto buffer_implementation =
            std::make_shared<rclcpp::intra_process_buffer::RingBufferImplementation<BufferT>>(
            buffer_size);

          // construct the intra_process_buffer
          buffer =
            std::make_shared<rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT,
              BufferT>>(buffer_implementation);

          break;
        }
      case IntraProcessBufferType::CallbackDefault:
        {
          throw std::runtime_error(
                  "IntraProcessBufferType::CallbackDefault should have been overwritten");
          break;
        }
      default:
        {
          throw std::runtime_error("Unrecognized IntraProcessBufferType value");
          break;
        }
    }

    // construct the subscription_intra_process
    std::shared_ptr<SubscriptionIntraProcessBase> subscription_intra_process =
      std::make_shared<SubscriptionIntraProcess<MessageT, Alloc>>(
      callback, buffer);

  return std::dynamic_pointer_cast<SubscriptionIntraProcess<MessageT, Alloc>>(
    subscription_intra_process);
}




}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_INTRA_PROCESS_HPP_
