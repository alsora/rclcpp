
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
#include "rclcpp/expand_topic_or_service_name.hpp"
#include "rclcpp/intra_process_manager.hpp"
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

  IntraProcessSubscriptionWaitableBase(){};

  size_t
  get_number_of_ready_guard_conditions() { return 1;}

  virtual bool
  add_to_wait_set(rcl_wait_set_t * wait_set) = 0;

  virtual bool
  is_ready(rcl_wait_set_t * wait_set) = 0;

  virtual void
  execute() = 0;

  rcl_guard_condition_t gc_;

};


/**
 *
 *
 * \typename CallbackMessageT is a raw message type such as sensor_msgs::msg::Image
 * \typename BufferT is the type that is stored in the buffer. It can be any of
 *  CallbackMessageT, std::shared_ptr<const CallbackMessageT> and std::unique_ptr<CallbackMessageT>
 *
 */
template<
  typename CallbackMessageT,
  typename BufferT = std::shared_ptr<const CallbackMessageT>,
  typename Alloc = std::allocator<void>>
class IntraProcessSubscriptionWaitable : public IntraProcessSubscriptionWaitableBase
{
public:

  using MessageAllocTraits = allocator::AllocRebind<CallbackMessageT, Alloc>;
  using MessageAlloc = typename MessageAllocTraits::allocator_type;
  using MessageDeleter = allocator::Deleter<MessageAlloc, CallbackMessageT>;
  using ConstMessageSharedPtr = std::shared_ptr<const CallbackMessageT>;
  using MessageUniquePtr = std::unique_ptr<CallbackMessageT, MessageDeleter>;

  using IntraProcessBufferT =
    rclcpp::intra_process_buffer::IntraProcessBuffer<CallbackMessageT, BufferT>;

  std::recursive_mutex reentrant_mutex_;


  std::shared_ptr<IntraProcessBufferT> queue_;
  AnySubscriptionCallback<CallbackMessageT, Alloc> * any_callback_;

  IntraProcessSubscriptionWaitable(
    AnySubscriptionCallback<CallbackMessageT, Alloc> * callback_ptr,
    std::shared_ptr<IntraProcessBufferT> queue_ptr)
  {
    any_callback_ = callback_ptr;
    queue_ = queue_ptr;

    std::shared_ptr<rclcpp::Context> context_ptr =
        rclcpp::contexts::default_context::get_global_default_context();

    rcl_guard_condition_options_t guard_condition_options =
        rcl_guard_condition_get_default_options();

    gc_ = rcl_get_zero_initialized_guard_condition();
    rcl_ret_t ret = rcl_guard_condition_init(
        &gc_, context_ptr->get_rcl_context().get(), guard_condition_options);

    if (RCL_RET_OK != ret){
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
  return queue_->hasData();
}

void execute()
{
  dispatch<BufferT>();
}

template <typename DestinationT>
typename std::enable_if<
(std::is_same<DestinationT, MessageUniquePtr>::value)>::type
dispatch()
{
  BufferT queue_msg;
  queue_->consume(queue_msg);
  any_callback_->dispatch_intra_process(std::move(queue_msg), rmw_message_info_t());
}

// This implementation works for both shared_ptr<MessageT> and MessageT
template <typename DestinationT>
typename std::enable_if<
(!std::is_same<DestinationT, MessageUniquePtr>::value)>::type
dispatch()
{
  BufferT queue_msg;
  queue_->consume(queue_msg);
  any_callback_->dispatch_intra_process(queue_msg, rmw_message_info_t());
}

};
}

#endif  // RCLCPP__SUBSCRIPTION_INTRA_PROCESS_WAITABLE_HPP_