
#ifndef RCLCPP__SUBSCRIPTION_IPC_WAITABLE_HPP_
#define RCLCPP__SUBSCRIPTION_IPC_WAITABLE_HPP_

#include <rmw/error_handling.h>
#include <rmw/rmw.h>

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>


#include "rcl/error_handling.h"
#include "rcl/subscription.h"

#include "rcl_interfaces/msg/intra_process_message.hpp"

#include "rclcpp/any_subscription_callback.hpp"
#include "rclcpp/contexts/default_context.hpp"
#include "rclcpp/exceptions.hpp"
#include "rclcpp/expand_topic_or_service_name.hpp"
#include "rclcpp/intra_process_manager.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/message_memory_strategy.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/subscription_traits.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/waitable.hpp"

#include "rclcpp/clock.hpp"
#include "rclcpp/time.hpp"

#include "rclcpp/intra_process_setting.hpp"

namespace rclcpp
{

namespace node_interfaces
{
class NodeTopicsInterface;
class NodeWaitablesInterface;
}  // namespace node_interfaces

template<
  typename CallbackMessageT,
  typename Alloc = std::allocator<void>>
class IPCSubscriptionWaitable : public rclcpp::Waitable
{
public:

  using ConstMessageSharedPtr = std::shared_ptr<const CallbackMessageT>;

  std::recursive_mutex reentrant_mutex_;

  rcl_guard_condition_t gc_;

  using QueueType = ConsumerProducerQueue<ConstMessageSharedPtr>;

  std::shared_ptr<QueueType> queue_;
  AnySubscriptionCallback<CallbackMessageT, Alloc> * any_callback_;

  ConstMessageSharedPtr shared_msg;

  IPCSubscriptionWaitable(){}

  void init(
    AnySubscriptionCallback<CallbackMessageT, Alloc> * callback_ptr,
    std::shared_ptr<QueueType> queue_ptr
  )
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
      std::cout<<"Error initializing guard condition IPC"<<std::endl;
    }

  }

size_t
get_number_of_ready_guard_conditions() { return 1;}

bool
add_to_wait_set(rcl_wait_set_t * wait_set)
{
  std::lock_guard<std::recursive_mutex> lock(reentrant_mutex_);

  rcl_ret_t ret = rcl_wait_set_add_guard_condition(wait_set, &gc_, NULL);
  return RCL_RET_OK == ret;
}

bool
is_ready(rcl_wait_set_t * wait_set) {
  (void)wait_set;

  return queue_->length() > 0;
}

void
execute()
{
  queue_->consume(shared_msg);

  if (any_callback_->use_take_shared_method()) {
    any_callback_->dispatch_intra_process(shared_msg, rmw_message_info_t());
  } else {
    auto unique_msg = std::make_unique<CallbackMessageT>(*shared_msg);
    any_callback_->dispatch_intra_process(std::move(unique_msg), rmw_message_info_t());
  }
}

};
}

#endif