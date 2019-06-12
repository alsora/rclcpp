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

#ifndef RCLCPP__BUFFERS__SIMPLE_QUEUE_IMPLEMENTATION_HPP_
#define RCLCPP__BUFFERS__SIMPLE_QUEUE_IMPLEMENTATION_HPP_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

#include "rclcpp/buffers/buffer_implementation_base.hpp"

namespace rclcpp
{
namespace intra_process_buffer
{

template<typename BufferT>
class SimpleQueueImplementation : public BufferImplementationBase<BufferT>
{
  std::condition_variable cond_;
  std::mutex mutex_;
  size_t max_size_;
  std::queue<BufferT> queue_;

public:
  explicit SimpleQueueImplementation(size_t max_size)
  : max_size_(max_size) {}

  void enqueue(BufferT request)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]()
      {return !is_full();});
    queue_.push(std::move(request));
    lock.unlock();
    cond_.notify_all();
  }

  void dequeue(BufferT & request)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]()
      {return !is_empty();});
    request = std::move(queue_.front());
    queue_.pop();
    lock.unlock();
    cond_.notify_all();
  }

  void clear()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!is_empty()) {
      queue_.pop();
    }
    lock.unlock();
    cond_.notify_all();
  }

  bool has_data() const
  {
    return queue_.size();
  }

  bool is_empty() const
  {
    return queue_.size() == 0;
  }

  bool is_full() const
  {
    return queue_.size() >= max_size_;
  }
};

}  // namespace intra_process_buffer
}  // namespace rclcpp

#endif  // RCLCPP__BUFFERS__SIMPLE_QUEUE_IMPLEMENTATION_HPP_
