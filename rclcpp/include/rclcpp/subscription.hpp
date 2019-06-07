// Copyright 2014 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__SUBSCRIPTION_HPP_
#define RCLCPP__SUBSCRIPTION_HPP_

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


#include "rclcpp/queues/cpqueue.hpp"

#include "rclcpp/subscription_intra_process_waitable.hpp"
#include "rclcpp/intra_process_setting.hpp"

#include <type_traits>

namespace rclcpp
{

namespace node_interfaces
{
class NodeTopicsInterface;
}  // namespace node_interfaces

// typename QueueT = std::shared_ptr<const CallbackMessageT>>
// typename QueueT = std::unique_ptr<CallbackMessageT, allocator::Deleter<typename allocator::AllocRebind<CallbackMessageT, Alloc>::allocator_type, CallbackMessageT>>>


/// Subscription implementation, templated on the type of message this subscription receives.
template<
  typename CallbackMessageT,
  typename Alloc = std::allocator<void>,
  typename QueueT = std::shared_ptr<const CallbackMessageT>>
class Subscription : public SubscriptionBase
{
  friend class rclcpp::node_interfaces::NodeTopicsInterface;

public:
  using MessageAllocTraits = allocator::AllocRebind<CallbackMessageT, Alloc>;
  using MessageAlloc = typename MessageAllocTraits::allocator_type;
  using MessageDeleter = allocator::Deleter<MessageAlloc, CallbackMessageT>;
  using ConstMessageSharedPtr = std::shared_ptr<const CallbackMessageT>;
  using MessageUniquePtr = std::unique_ptr<CallbackMessageT, MessageDeleter>;

  RCLCPP_SMART_PTR_DEFINITIONS(Subscription)

  static_assert(std::is_same<QueueT, CallbackMessageT>::value ||
                std::is_same<QueueT, ConstMessageSharedPtr>::value ||
                std::is_same<QueueT, MessageUniquePtr>::value
                , "QueueT is not a valid type");

  /// Default constructor.
  /**
   * The constructor for a subscription is almost never called directly. Instead, subscriptions
   * should be instantiated through Node::create_subscription.
   * \param[in] node_handle rcl representation of the node that owns this subscription.
   * \param[in] type_support_handle rosidl type support struct, for the Message type of the topic.
   * \param[in] topic_name Name of the topic to subscribe to.
   * \param[in] subscription_options options for the subscription.
   * \param[in] callback User defined callback to call when a message is received.
   * \param[in] memory_strategy The memory strategy to be used for managing message memory.
   */
  Subscription(
    std::shared_ptr<rcl_node_t> node_handle,
    const rosidl_message_type_support_t & type_support_handle,
    const std::string & topic_name,
    const rcl_subscription_options_t & subscription_options,
    AnySubscriptionCallback<CallbackMessageT, Alloc> callback,
    const SubscriptionEventCallbacks & event_callbacks,
    typename message_memory_strategy::MessageMemoryStrategy<CallbackMessageT, Alloc>::SharedPtr
    memory_strategy = message_memory_strategy::MessageMemoryStrategy<CallbackMessageT,
    Alloc>::create_default())
  : SubscriptionBase(
      node_handle,
      type_support_handle,
      topic_name,
      subscription_options,
      rclcpp::subscription_traits::is_serialized_subscription_argument<CallbackMessageT>::value),
    any_callback_(callback),
    message_memory_strategy_(memory_strategy)
  {
    if (event_callbacks.deadline_callback) {
      this->add_event_handler(event_callbacks.deadline_callback,
        RCL_SUBSCRIPTION_REQUESTED_DEADLINE_MISSED);
    }
    if (event_callbacks.liveliness_callback) {
      this->add_event_handler(event_callbacks.liveliness_callback,
        RCL_SUBSCRIPTION_LIVELINESS_CHANGED);
    }
  }

  /// Support dynamically setting the message memory strategy.
  /**
   * Behavior may be undefined if called while the subscription could be executing.
   * \param[in] message_memory_strategy Shared pointer to the memory strategy to set.
   */
  void set_message_memory_strategy(
    typename message_memory_strategy::MessageMemoryStrategy<CallbackMessageT,
    Alloc>::SharedPtr message_memory_strategy)
  {
    message_memory_strategy_ = message_memory_strategy;
  }

  std::shared_ptr<void> create_message()
  {
    /* The default message memory strategy provides a dynamically allocated message on each call to
     * create_message, though alternative memory strategies that re-use a preallocated message may be
     * used (see rclcpp/strategies/message_pool_memory_strategy.hpp).
     */
    return message_memory_strategy_->borrow_message();
  }

  std::shared_ptr<rcl_serialized_message_t> create_serialized_message()
  {
    return message_memory_strategy_->borrow_serialized_message();
  }

  void handle_message(std::shared_ptr<void> & message, const rmw_message_info_t & message_info)
  {
    if (matches_any_intra_process_publishers(&message_info.publisher_gid)) {
      // In this case, the message will be delivered via intra process and
      // we should ignore this copy of the message.
      return;
    }
    auto typed_message = std::static_pointer_cast<CallbackMessageT>(message);
    any_callback_.dispatch(typed_message, message_info);
  }

  /// Return the loaned message.
  /** \param message message to be returned */
  void return_message(std::shared_ptr<void> & message)
  {
    auto typed_message = std::static_pointer_cast<CallbackMessageT>(message);
    message_memory_strategy_->return_message(typed_message);
  }

  void return_serialized_message(std::shared_ptr<rcl_serialized_message_t> & message)
  {
    message_memory_strategy_->return_serialized_message(message);
  }

  void create_intra_process_tools()
  {
    // this is just a quick hack, I should build them here
    message_allocator_ = any_callback_.get_message_allocator();
    message_deleter_ = any_callback_.get_message_deleter();

    // construct the queue
    typed_queue = std::make_shared<ConsumerProducerQueue<QueueT>>(100);

    waitable_ptr = std::make_shared<IntraProcessSubscriptionWaitable<CallbackMessageT,QueueT, Alloc>>();
    waitable_ptr->init(&any_callback_, typed_queue);
  }

  std::shared_ptr<rclcpp::Waitable>
  get_intra_process_waitable()
  {
    return waitable_ptr;
  }

  bool
  use_take_shared_method()
  {
    return any_callback_.use_take_shared_method();
  }

  /**
   * Adds a std::shared_ptr<const void> message to the intra-process communication buffer of this Subscription.
   * The message has to be converted into the QueueT type.
   * The message has been shared with the Intra Process Manager that does not own it.
   * If the subscription wants ownership it's always necessary to make a copy
   */
  void add_shared_message_to_buffer(std::shared_ptr<const void> shared_msg)
  {
    add_shared_message_to_buffer_impl<QueueT>(shared_msg);
    auto ret = rcl_trigger_guard_condition(&waitable_ptr->gc_);
    (void)ret;
  }

  /**
   * Adds a void* message to the intra-process communication buffer of this Subscription.
   * The message has to be converted into the QueueT type.
   * The message is owned by the Intra Process Manager that can give up ownership if needed.
   * The intra-process manager tells the subscription if the message can be taken or if a copy is needed
   */
  void add_owned_message_to_buffer(void* msg, bool can_be_taken)
  {
    add_owned_message_to_buffer_impl<QueueT>(msg, can_be_taken);
    auto ret = rcl_trigger_guard_condition(&waitable_ptr->gc_);
    (void)ret;
  }

  // shared_ptr to ConstMessageSharedPtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_shared_message_to_buffer_impl(std::shared_ptr<const void> shared_msg)
  {
    auto msg = std::static_pointer_cast<const CallbackMessageT>(shared_msg);
    typed_queue->add(msg);
  }

  // shared_ptr to MessageUniquePtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_shared_message_to_buffer_impl(std::shared_ptr<const void> shared_msg)
  {
    auto casted_msg_ptr = std::static_pointer_cast<const CallbackMessageT>(shared_msg);
    auto ptr = MessageAllocTraits::allocate(*message_allocator_.get(), 1);
    MessageAllocTraits::construct(*message_allocator_.get(), ptr, *casted_msg_ptr);
    MessageUniquePtr unique_msg(ptr, message_deleter_);
    typed_queue->move_in(std::move(unique_msg));
  }


  // shared_ptr to CallbackMessageT
  template <typename DestinationT>
  typename std::enable_if<
  !std::is_same<DestinationT, ConstMessageSharedPtr>::value
  &&
  !std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_shared_message_to_buffer_impl(std::shared_ptr<const void> shared_msg)
  {
    auto shared_casted_msg = std::static_pointer_cast<const CallbackMessageT>(shared_msg);
    CallbackMessageT msg = *shared_casted_msg;
    typed_queue->add(msg);
  }

  // void* to ConstMessageSharedPtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_owned_message_to_buffer_impl(void* msg, bool can_be_taken)
  {
    if (can_be_taken){
      ConstMessageSharedPtr shared_msg(static_cast<CallbackMessageT*>(msg));
      typed_queue->add(shared_msg);
    }
    else{
      CallbackMessageT* casted_msg_ptr = static_cast<CallbackMessageT*>(msg);
      auto ptr = MessageAllocTraits::allocate(*message_allocator_.get(), 1);
      MessageAllocTraits::construct(*message_allocator_.get(), ptr, *casted_msg_ptr);
      ConstMessageSharedPtr shared_msg(ptr);
      typed_queue->add(shared_msg);
    }
  }

  // void* to MessageUniquePtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_owned_message_to_buffer_impl(void* msg, bool can_be_taken)
  {
    if (can_be_taken){
      MessageUniquePtr unique_msg(static_cast<CallbackMessageT*>(msg));
      typed_queue->move_in(std::move(unique_msg));
    }
    else{
      CallbackMessageT* casted_msg_ptr = static_cast<CallbackMessageT*>(msg);
      auto ptr = MessageAllocTraits::allocate(*message_allocator_.get(), 1);
      MessageAllocTraits::construct(*message_allocator_.get(), ptr, *casted_msg_ptr);
      MessageUniquePtr unique_msg(ptr, message_deleter_);
      typed_queue->move_in(std::move(unique_msg));
    }
  }

  // void* to CallbackMessageT
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, CallbackMessageT>::value
  >::type
  add_owned_message_to_buffer_impl(void* msg, bool can_be_taken)
  {
    (void)can_be_taken;
    CallbackMessageT* casted_message = static_cast<CallbackMessageT*>(msg);
    typed_queue->add(*casted_message);
  }

private:

  std::shared_ptr<ConsumerProducerQueue<QueueT> > typed_queue;
  std::shared_ptr<IntraProcessSubscriptionWaitable<CallbackMessageT, QueueT, Alloc>> waitable_ptr;

  std::shared_ptr<MessageAlloc> message_allocator_;
  MessageDeleter message_deleter_;

  bool
  matches_any_intra_process_publishers(const rmw_gid_t * sender_gid)
  {
    if (!use_intra_process_) {
      return false;
    }
    auto ipm = weak_ipm_.lock();
    if (!ipm) {
      throw std::runtime_error(
              "intra process publisher check called "
              "after destruction of intra process manager");
    }
    return ipm->matches_any_publishers(sender_gid);
  }

  RCLCPP_DISABLE_COPY(Subscription)

  AnySubscriptionCallback<CallbackMessageT, Alloc> any_callback_;
  typename message_memory_strategy::MessageMemoryStrategy<CallbackMessageT, Alloc>::SharedPtr
    message_memory_strategy_;
};

}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_HPP_
