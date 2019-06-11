#ifndef __QUEUES_INTRA_PROCESS_BUFFER_HPP__
#define __QUEUES_INTRA_PROCESS_BUFFER_HPP__

#include <queue>
#include <mutex>
#include <condition_variable>


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

  virtual bool isEmpty() const = 0;
  virtual bool isFull() const = 0;
  virtual bool hasData() const = 0;
};


template<
  typename MessageT,
  typename BufferT = MessageT>
class IntraProcessBuffer : public IntraProcessBufferBase
{
  std::condition_variable cond;
  std::mutex mutex;
  size_t maxSize;
  std::queue<BufferT> cpq;

public:
  //RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessBuffer<MessageT, BufferT>)
  using ConstMessageSharedPtr = std::shared_ptr<const MessageT>;
  using MessageUniquePtr = std::unique_ptr<MessageT>;

  static_assert(std::is_same<BufferT, MessageT>::value ||
              std::is_same<BufferT, ConstMessageSharedPtr>::value ||
              std::is_same<BufferT, MessageUniquePtr>::value,
              "BufferT is not a valid type");

  IntraProcessBuffer(size_t mxsz)
  : maxSize(mxsz)
  { }

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

  void consume(BufferT &request)
  {
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait(lock, [this]()
        { return !isEmpty(); });
    request = std::move(cpq.front());
    cpq.pop();
    lock.unlock();
    cond.notify_all();
  }

  bool isFull() const
  {
    return cpq.size() >= maxSize;
  }

  bool isEmpty() const
  {
    return cpq.size() == 0;
  }

  bool hasData() const
  {
    return cpq.size();
  }

  int length() const
  {
    return cpq.size();
  }

  void clear()
  {
    std::unique_lock<std::mutex> lock(mutex);
    while (!isEmpty())
    {
        cpq.pop();
    }
    lock.unlock();
    cond.notify_all();
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
    add_impl(shared_casted_msg);
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
    add_impl(msg);
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
    add_impl(std::move(unique_msg));
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
    add_impl(*casted_message);
  }

  /**
   * Adds a BufferT message to the intra-process communication buffer.
   */
  void add_impl(BufferT request)
  {
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait(lock, [this]()
        { return !isFull(); });
    cpq.push(std::move(request));
    lock.unlock();
    cond.notify_all();
  }

};

}
}


#endif  //__QUEUES_INTRA_PROCESS_BUFFER_HPP__
