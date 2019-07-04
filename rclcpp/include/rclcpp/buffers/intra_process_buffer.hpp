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

#ifndef RCLCPP__BUFFERS__INTRA_PROCESS_BUFFER_HPP_
#define RCLCPP__BUFFERS__INTRA_PROCESS_BUFFER_HPP_

#include <memory>
#include <type_traits>
#include <utility>

#include "rclcpp/buffers/buffer_implementation_base.hpp"

namespace rclcpp
{
namespace intra_process_buffer
{

class IntraProcessBufferBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessBufferBase)

  virtual void clear() = 0;

  virtual bool has_data() const = 0;
  virtual bool use_take_shared_method() const = 0;
};

template<typename MessageT>
class IntraProcessBuffer : public IntraProcessBufferBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessBuffer)

  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT>;

  virtual void add(ConstMessageSharedPtr msg) = 0;
  virtual void add(MessageUniquePtr msg) = 0;

  virtual void consume(ConstMessageSharedPtr & msg) = 0;
  virtual void consume(MessageUniquePtr & msg) = 0;
};

template<
  typename MessageT,
  typename BufferT = std::unique_ptr<MessageT>>
class TypedIntraProcessBuffer : public IntraProcessBuffer<MessageT>
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(TypedIntraProcessBuffer)

  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT>;
  static_assert(std::is_same<BufferT, ConstMessageSharedPtr>::value ||
    std::is_same<BufferT, MessageUniquePtr>::value,
    "BufferT is not a valid type");

  TypedIntraProcessBuffer(
    std::shared_ptr<BufferImplementationBase<BufferT>> buffer_impl)
  {
    buffer_ = buffer_impl;
  }

  void add(ConstMessageSharedPtr msg)
  {
    add_message<BufferT>(std::move(msg));
  }

  void add(MessageUniquePtr msg)
  {
    add_message<BufferT>(std::move(msg));
  }

  void consume(ConstMessageSharedPtr & msg)
  {
    consume_message<BufferT>(msg);
  }

  void consume(MessageUniquePtr & msg)
  {
    consume_message<BufferT>(msg);
  }

  bool has_data() const
  {
    return buffer_->has_data();
  }

  void clear()
  {
    buffer_->clear();
  }

  bool use_take_shared_method() const
  {
    if (std::is_same<BufferT, ConstMessageSharedPtr>::value) {
      return true;
    } else {
      return false;
    }
  }

private:
  std::shared_ptr<BufferImplementationBase<BufferT>> buffer_;

  // ConstMessageSharedPtr to ConstMessageSharedPtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_message(ConstMessageSharedPtr shared_msg)
  {
    buffer_->enqueue(std::move(shared_msg));
  }

  // ConstMessageSharedPtr to MessageUniquePtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_message(ConstMessageSharedPtr shared_msg)
  {
    auto unique_msg = std::make_unique<MessageT>(*shared_msg);
    buffer_->enqueue(std::move(unique_msg));
  }

  // MessageUniquePtr to ConstMessageSharedPtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_message(MessageUniquePtr unique_msg)
  {
    buffer_->enqueue(std::move(unique_msg));
  }

  // MessageUniquePtr to MessageUniquePtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_message(MessageUniquePtr unique_msg)
  {
    buffer_->enqueue(std::move(unique_msg));
  }

  // ConstMessageSharedPtr to ConstMessageSharedPtr
  template<typename OriginT>
  typename std::enable_if<
    std::is_same<OriginT, ConstMessageSharedPtr>::value
  >::type
  consume_message(ConstMessageSharedPtr & shared_msg)
  {
    buffer_->dequeue(shared_msg);
  }

  // MessageUniquePtr to ConstMessageSharedPtr
  template<typename OriginT>
  typename std::enable_if<
    std::is_same<OriginT, MessageUniquePtr>::value
  >::type
  consume_message(ConstMessageSharedPtr & shared_msg)
  {
    MessageUniquePtr buffer_msg;
    buffer_->dequeue(buffer_msg);
    shared_msg = std::move(buffer_msg);
  }

  // ConstMessageSharedPtr to MessageUniquePtr
  template<typename OriginT>
  typename std::enable_if<
    std::is_same<OriginT, ConstMessageSharedPtr>::value
  >::type
  consume_message(MessageUniquePtr & unique_msg)
  {
    ConstMessageSharedPtr buffer_msg;
    buffer_->dequeue(buffer_msg);
    unique_msg = std::make_unique<MessageT>(*buffer_msg);
  }

  // MessageUniquePtr to MessageUniquePtr
  template<typename OriginT>
  typename std::enable_if<
    std::is_same<OriginT, MessageUniquePtr>::value
  >::type
  consume_message(MessageUniquePtr & unique_msg)
  {
    buffer_->dequeue(unique_msg);
  }
};

}  // namespace intra_process_buffer
}  // namespace rclcpp


#endif  // RCLCPP__BUFFERS__INTRA_PROCESS_BUFFER_HPP_
