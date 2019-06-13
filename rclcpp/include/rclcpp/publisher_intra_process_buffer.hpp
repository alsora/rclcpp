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

#ifndef RCLCPP__PUBLISHER_INTRA_PROCESS_BUFFER_HPP_
#define RCLCPP__PUBLISHER_INTRA_PROCESS_BUFFER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rclcpp/allocator/allocator_common.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{

class PublisherIntraProcessBufferBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(PublisherIntraProcessBufferBase)
};

template<typename T, typename Alloc = std::allocator<void>>
class PublisherIntraProcessBuffer : public PublisherIntraProcessBufferBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(PublisherIntraProcessBuffer<T, Alloc>)
  using ElemAllocTraits = allocator::AllocRebind<T, Alloc>;
  using ElemAlloc = typename ElemAllocTraits::allocator_type;
  using ElemDeleter = allocator::Deleter<ElemAlloc, T>;
  using ConstElemSharedPtr = std::shared_ptr<const T>;
  using ElemUniquePtr = std::unique_ptr<T, ElemDeleter>;

  using VectorAlloc =
    typename std::allocator_traits<Alloc>::template rebind_alloc<ConstElemSharedPtr>;

  explicit PublisherIntraProcessBuffer(size_t size)
  : elements_(size), head_(0)
  {
    if (size == 0) {
      throw std::invalid_argument("size must be a positive, non-zero value");
    }
  }

  virtual ~PublisherIntraProcessBuffer() {}

  /// Adds a ConstElemSharedPtr message to the ring buffer.
  /**
   * This is the only data-type that can be inserted into the buffer.
   * The reason is that this is used for transient local durability QoS
   * and this QoS requires that the publisher publishes both inter and intra-process.
   * Thus std::unique_ptr<MessageT> are always promoted into shared_ptr.
   */
  void
  add(ConstElemSharedPtr value)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    elements_[head_] = value;
    head_++;
    if (head_ > elements_.size()) {
      wrapped_ = true;
      head_ = head_ % elements_.size();
    }
  }

  /// Returns all the elements stored in the buffer.
  /**
   * Elements are ordered from the oldest to the most recent.
   * The size of the returned vector is not fixed until the buffer wraps itself.
   */
  std::vector<ConstElemSharedPtr, VectorAlloc>
  get_all()
  {
    if (wrapped_) {
      std::vector<ConstElemSharedPtr, VectorAlloc> output(elements_.size());
      auto pivot = elements_.begin() + head_;
      std::rotate_copy(elements_.begin(), pivot, elements_.end(), output.begin());
      return output;
    } else {
      auto first = elements_.begin();
      auto last = elements_.begin() + head_;
      std::vector<ConstElemSharedPtr, VectorAlloc> output(first, last);
      return output;
    }
  }

private:
  std::vector<ConstElemSharedPtr, VectorAlloc> elements_;
  size_t head_;
  bool wrapped_;
  std::mutex data_mutex_;
};

} // namespace rclcpp

#endif  // RCLCPP__PUBLISHER_INTRA_PROCESS_BUFFER_HPP_
