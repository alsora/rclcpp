
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

#ifndef RCLCPP__CREATE_INTRA_PROCESS_BUFFER_HPP_
#define RCLCPP__CREATE_INTRA_PROCESS_BUFFER_HPP_

#include <memory>
#include <type_traits>
#include <utility>

#include "rcl/subscription.h"

#include "rclcpp/buffers/ring_buffer_implementation.hpp"
#include "rclcpp/buffers/intra_process_buffer.hpp"
#include "rclcpp/intra_process_buffer_type.hpp"

namespace rclcpp
{

  template<
    typename MessageT,
    typename Alloc>
  typename intra_process_buffer::IntraProcessBuffer<MessageT>::SharedPtr
  create_intra_process_buffer(
    IntraProcessBufferType buffer_type,
    const rcl_subscription_options_t & options
  )
  {
    using MessageAllocTraits = allocator::AllocRebind<MessageT, Alloc>;
    using MessageAlloc = typename MessageAllocTraits::allocator_type;
    using MessageDeleter = allocator::Deleter<MessageAlloc, MessageT>;
    using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
    using MessageUniquePtr = std::unique_ptr<MessageT, MessageDeleter>;

    size_t buffer_size = options.qos.depth;

    typename intra_process_buffer::IntraProcessBuffer<MessageT>::SharedPtr buffer;

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
      default:
        {
          throw std::runtime_error("Unrecognized IntraProcessBufferType value");
          break;
        }
    }

    return buffer;
  }

}  // namespace rclcpp

#endif // RCLCPP__CREATE_INTRA_PROCESS_BUFFER_HPP_
