#ifndef __QUEUES_SIMPLE_QUEUE_HPP__
#define __QUEUES_SIMPLE_QUEUE_HPP__

#include <condition_variable>
#include <mutex>
#include <queue>

#include "rclcpp/buffers/buffer_implementation_base.hpp"

namespace rclcpp
{
namespace intra_process_buffer
{

template<typename BufferT>
class SimpleQueueImplementation : public BufferImplementationBase<BufferT>
{
  std::condition_variable cond;
  std::mutex mutex;
  size_t maxSize;
  std::queue<BufferT> cpq;

public:

  SimpleQueueImplementation(size_t max_size)
  : maxSize(max_size){}

  void enqueue(BufferT request)
  {
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait(lock, [this]()
        { return !isFull(); });
    cpq.push(std::move(request));
    lock.unlock();
    cond.notify_all();
  }

  void dequeue(BufferT &request)
  {
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait(lock, [this]()
        { return !isEmpty(); });
    request = std::move(cpq.front());
    cpq.pop();
    lock.unlock();
    cond.notify_all();
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

  bool hasData() const
  {
    return cpq.size();
  }

  bool isEmpty() const
  {
    return cpq.size() == 0;
  }

  bool isFull() const
  {
    return cpq.size() >= maxSize;
  }

};

}
}

#endif // __QUEUES_SIMPLE_QUEUE_HPP__
