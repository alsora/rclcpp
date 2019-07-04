// Copyright 2016 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__SUBSCRIPTION_FACTORY_HPP_
#define RCLCPP__SUBSCRIPTION_FACTORY_HPP_

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "rcl/subscription.h"

#include "rosidl_typesupport_cpp/message_type_support.hpp"

#include "rclcpp/create_intra_process_buffer.hpp"
#include "rclcpp/subscription.hpp"
#include "rclcpp/subscription_traits.hpp"
#include "rclcpp/intra_process_buffer_type.hpp"
#include "rclcpp/intra_process_manager.hpp"
#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{

/// Factory with functions used to create a Subscription<MessageT>.
/**
 * This factory class is used to encapsulate the template generated functions
 * which are used during the creation of a Message type specific subscription
 * within a non-templated class.
 *
 * It is created using the create_subscription_factory function, which is
 * usually called from a templated "create_subscription" method of the Node
 * class, and is passed to the non-templated "create_subscription" method of
 * the NodeTopics class where it is used to create and setup the Subscription.
 */
struct SubscriptionFactory
{
  // Creates a Subscription<MessageT> object and returns it as a SubscriptionBase.
  using SubscriptionFactoryFunction = std::function<
    rclcpp::SubscriptionBase::SharedPtr(
      rclcpp::node_interfaces::NodeBaseInterface * node_base,
      const std::string & topic_name,
      const rcl_subscription_options_t & subscription_options)>;

  SubscriptionFactoryFunction create_typed_subscription;

  // Creates a SubscriptionIntraProcess<MessageT> object and returns it as a base.
  using SubscriptionIntraProcessFactoryFunction =
    std::function<rclcpp::SubscriptionIntraProcessBase::SharedPtr(
      rclcpp::SubscriptionBase::SharedPtr sub_base,
      rclcpp::IntraProcessBufferType buffer_type,
      const rcl_subscription_options_t & subscription_options)>;

  SubscriptionIntraProcessFactoryFunction create_typed_subscription_intra_process;
};

/// Return a SubscriptionFactory with functions for creating a SubscriptionT<MessageT, Alloc>.
template<
  typename MessageT,
  typename CallbackT,
  typename Alloc,
  typename CallbackMessageT,
  typename SubscriptionT>
SubscriptionFactory
create_subscription_factory(
  CallbackT && callback,
  const SubscriptionEventCallbacks & event_callbacks,
  typename rclcpp::message_memory_strategy::MessageMemoryStrategy<
    CallbackMessageT, Alloc>::SharedPtr
  msg_mem_strat,
  std::shared_ptr<Alloc> allocator)
{
  SubscriptionFactory factory;

  using rclcpp::AnySubscriptionCallback;
  AnySubscriptionCallback<CallbackMessageT, Alloc> any_subscription_callback(allocator);
  any_subscription_callback.set(std::forward<CallbackT>(callback));

  auto message_alloc =
    std::make_shared<typename Subscription<CallbackMessageT, Alloc>::MessageAlloc>();

  // factory function that creates a MessageT specific SubscriptionT
  factory.create_typed_subscription =
    [allocator, msg_mem_strat, any_subscription_callback, event_callbacks, message_alloc](
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    const std::string & topic_name,
    const rcl_subscription_options_t & subscription_options
    ) -> rclcpp::SubscriptionBase::SharedPtr
    {
      auto options_copy = subscription_options;
      options_copy.allocator =
        rclcpp::allocator::get_rcl_allocator<CallbackMessageT>(*message_alloc.get());

      using rclcpp::Subscription;
      using rclcpp::SubscriptionBase;

      auto sub = Subscription<CallbackMessageT, Alloc>::make_shared(
        node_base->get_shared_rcl_node_handle(),
        *rosidl_typesupport_cpp::get_message_type_support_handle<MessageT>(),
        topic_name,
        options_copy,
        any_subscription_callback,
        event_callbacks,
        msg_mem_strat);
      auto sub_base_ptr = std::dynamic_pointer_cast<SubscriptionBase>(sub);
      return sub_base_ptr;
    };

  factory.create_typed_subscription_intra_process =
    [](
    rclcpp::SubscriptionBase::SharedPtr sub_base,
    rclcpp::IntraProcessBufferType buffer_type,
    const rcl_subscription_options_t & subscription_options
    ) -> rclcpp::SubscriptionIntraProcessBase::SharedPtr
    {
      // If the user has not specified a type for the intra-process buffer, use the callback one.
      if (buffer_type == IntraProcessBufferType::CallbackDefault) {
        buffer_type = sub_base->use_take_shared_method() ?
          IntraProcessBufferType::SharedPtr : IntraProcessBufferType::UniquePtr;
      }
      // Create the intra-process buffer.
      auto buffer = rclcpp::create_intra_process_buffer<MessageT, Alloc>(
        buffer_type,
        subscription_options);

      using rclcpp::SubscriptionIntraProcessBase;

      auto sub =
        std::static_pointer_cast<SubscriptionT>(sub_base);

      auto sub_intra_process =
        std::make_shared<SubscriptionIntraProcess<MessageT, Alloc>>(sub, buffer);

      auto sub_intra_process_base_ptr =
        std::dynamic_pointer_cast<SubscriptionIntraProcessBase>(sub_intra_process);

      return sub_intra_process_base_ptr;
    };

  // return the factory now that it is populated
  return factory;
}

}  // namespace rclcpp

#endif  // RCLCPP__SUBSCRIPTION_FACTORY_HPP_
