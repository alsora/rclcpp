#ifndef __INTRA_PROCESS_BUFFER_HPP__
#define __INTRA_PROCESS_BUFFER_HPP__

#include <memory>
#include <type_traits>

#include "rclcpp/buffers/buffer_implementation_base.hpp"

namespace rclcpp
{
namespace intra_process_buffer
{

class IntraProcessBufferBase
{
public:
  //RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessBufferBase)

  virtual void add(std::shared_ptr<const void> shared_msg) = 0;
  virtual void add(void* msg) = 0;

  virtual bool hasData() const = 0;
  virtual void clear() = 0;
};


template<
  typename MessageT,
  typename BufferT = MessageT>
class IntraProcessBuffer : public IntraProcessBufferBase
{
  std::shared_ptr<BufferImplementationBase<BufferT>> buffer_;
  //BufferImplementationBase<BufferT>::SharedPtr buffer_;

public:
  //RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessBuffer<MessageT, BufferT>)
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
  void add(void* msg)
  {
    add_owned_message<BufferT>(msg);
  }

  void consume(BufferT &msg)
  {
    buffer_->dequeue(msg);
  }

  bool hasData() const
  {
    return buffer_->hasData();
  }

  void clear()
  {
    buffer_->clear();
  }

private:

  // shared_ptr to ConstMessageSharedPtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_shared_message(std::shared_ptr<const void> shared_msg)
  {
    auto shared_casted_msg = std::static_pointer_cast<const MessageT>(shared_msg);
    buffer_->enqueue(shared_casted_msg);
  }

  // shared_ptr to MessageUniquePtr
  template <typename DestinationT>
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
  template <typename DestinationT>
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
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, ConstMessageSharedPtr>::value
  >::type
  add_owned_message(void* msg)
  {
    (void)msg;
    /**
     * This function should not be used.
     */
    throw std::runtime_error("add_owned_message for ConstMessageSharedPtr buffer");
  }

  // void* to MessageUniquePtr
  template <typename DestinationT>
  typename std::enable_if<
  std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_owned_message(void* msg)
  {
    MessageUniquePtr unique_msg(static_cast<MessageT*>(msg));
    buffer_->enqueue(std::move(unique_msg));
  }

  // void* to CallbackMessageT
  template <typename DestinationT>
  typename std::enable_if<
  !std::is_same<DestinationT, ConstMessageSharedPtr>::value
  &&
  !std::is_same<DestinationT, MessageUniquePtr>::value
  >::type
  add_owned_message(void* msg)
  {
    MessageT* casted_message = static_cast<MessageT*>(msg);
    buffer_->enqueue(*casted_message);
  }

};

}
}


#endif  //__INTRA_PROCESS_BUFFER_HPP__
