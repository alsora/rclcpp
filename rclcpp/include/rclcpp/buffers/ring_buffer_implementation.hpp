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

#ifndef RCLCPP__BUFFERS_RING_BUFFER_IMPLEMENTATION_HPP_
#define RCLCPP__BUFFERS_RING_BUFFER_IMPLEMENTATION_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rclcpp/macros.hpp"
#include "rclcpp/visibility_control.hpp"

#include "iostream"

// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

namespace rclcpp
{
namespace intra_process_buffer
{

template<typename BufferT>
class RingBufferImplementation : public BufferImplementationBase<BufferT>
{
public:

  explicit RingBufferImplementation(size_t size)
  : ring_buffer_(size), read_(0), write_(0)
  {
    size_ = ring_buffer_.size();

    if (size_ == 0) {
      throw std::invalid_argument("size must be a positive, non-zero value");
    }
  }

  virtual ~RingBufferImplementation() {}

  void enqueue(BufferT request)
  {
    assert(!is_full());
    std::unique_lock<std::mutex> lock(mutex_);

    ring_buffer_[write_] = std::move(request);
    write_++;
    if (write_ == size_) {
      write_ = 0;
    }

    lock.unlock();
  }

  void dequeue(BufferT & request)
  {
    assert(has_data());
    std::unique_lock<std::mutex> lock(mutex_);

    request = std::move(ring_buffer_[read_]);
    read_++;
    if (read_ == size_) {
      read_ = 0;
    }

    lock.unlock();
  }

  bool has_data() const
  {
    return read_ != write_;
  }

  void clear()
  {
    return;
  }

  uint32_t size()
  {
    return write_ - read_;
  }

  bool is_full()
  {
    return size() == size_;
  }

private:
  std::vector<BufferT> ring_buffer_;
  size_t size_;
  uint32_t read_;
  uint32_t write_;

  std::mutex mutex_;
};

}  // namespace intra_process_buffer
} // namespace rclcpp

#endif  // RCLCPP__BUFFERS_RING_BUFFER_IMPLEMENTATION_HPP_
