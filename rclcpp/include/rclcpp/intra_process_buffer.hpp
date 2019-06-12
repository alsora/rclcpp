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

#ifndef RCLCPP__INTRA_PROCESS_BUFFER_HPP_
#define RCLCPP__INTRA_PROCESS_BUFFER_HPP_

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
  virtual void add(std::shared_ptr<const void> shared_msg) = 0;
  virtual void add(void * msg) = 0;

  virtual bool has_data() const = 0;
  virtual void clear() = 0;
};


template<
  typename MessageT,
  typename BufferT = MessageT>
class IntraProcessBuffer : public IntraProcessBufferBase
{
public:
  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT>;

  static_assert(std::is_same<BufferT, MessageT>::value ||
    std::is_same<BufferT, ConstMessageSharedPtr>::value ||
    std::is_same<BufferT, MessageUniquePtr>::value,
    "BufferT is not a valid type");

  IntraProcessBuffer(
    std::shared_ptr<BufferImplementationBase<BufferT>> buffer_impl)
  {
    buffer_ = buffer_impl;
  }

  /**
   * Adds a std::shared_ptr<const void> message to the intra-process communication buffer.
   * The message has to be converted into the BufferT type.
   * The message has been shared with this object that does not own it.
   * If the BufferT requires ownership it's necessary to make a copy
   */
  void add(std::shared_ptr<const void> shared_msg)
  {
    add_shared_message<BufferT>(shared_msg);
  }

  /**
   * Adds a void* message to the intra-process communication buffer.
   * The message has to be converted into the BufferT type.
   * The ownership of the message has been given to this object.
   */
  void add(void * msg)
  {
    add_owned_message<BufferT>(msg);
  }

  void consume(BufferT & msg)
  {
    buffer_->dequeue(msg);
  }

  bool has_data() const
  {
    return buffer_->has_data();
  }

  void clear()
  {
    buffer_->clear();
  }

private:

  std::shared_ptr<BufferImplementationBase<BufferT>> buffer_;

  // shared_ptr to ConstMessageSharedPtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_shared_message(std::shared_ptr<const void> shared_msg)
  {
    auto shared_casted_msg = std::static_pointer_cast<const MessageT>(shared_msg);
    buffer_->enqueue(shared_casted_msg);
  }

  // shared_ptr to MessageUniquePtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_shared_message(std::shared_ptr<const void> shared_msg)
  {
    (void)shared_msg;
    /**
     * This function should not be used.
     */
    throw std::runtime_error("add_shared_message for MessageUniquePtr buffer");
  }

  // shared_ptr to MessageT
  template<typename DestinationT>
  typename std::enable_if<
    !std::is_same<DestinationT, ConstMessageSharedPtr>::value
    &&
    !std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_shared_message(std::shared_ptr<const void> shared_msg)
  {
    auto shared_casted_msg = std::static_pointer_cast<const MessageT>(shared_msg);
    MessageT msg = *shared_casted_msg;
    buffer_->enqueue(msg);
  }

  // void* to ConstMessageSharedPtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_owned_message(void * msg)
  {
    (void)msg;
    /**
     * This function should not be used.
     */
    throw std::runtime_error("add_owned_message for ConstMessageSharedPtr buffer");
  }

  // void* to MessageUniquePtr
  template<typename DestinationT>
  typename std::enable_if<
    std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_owned_message(void * msg)
  {
    MessageUniquePtr unique_msg(static_cast<MessageT *>(msg));
    buffer_->enqueue(std::move(unique_msg));
  }

  // void* to CallbackMessageT
  template<typename DestinationT>
  typename std::enable_if<
    !std::is_same<DestinationT, ConstMessageSharedPtr>::value
    &&
    !std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_owned_message(void * msg)
  {
    MessageT * casted_message = static_cast<MessageT *>(msg);
    buffer_->enqueue(*casted_message);
  }
};

}  // namespace intra_process_buffer
}  // namespace rclcpp


#endif  // RCLCPP__INTRA_PROCESS_BUFFER_HPP_
