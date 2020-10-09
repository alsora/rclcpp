// Copyright 2020 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_
#define RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_

#include <chrono>
#include <vector>

#include <rclcpp/timer.hpp>

using namespace std::chrono_literals;

namespace rclcpp
{
namespace executors
{

class TimersManager
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(TimersManager)

  /**
   * @brief Construct a new Timers Heap object
   */
  TimersManager()
  {
    size = 0;
  }

  /**
   * @brief Adds a new TimerBase to the heap
   * @param timer the timer to be added
   */
  inline void add_timer(rclcpp::TimerBase::SharedPtr timer)
  {
    // Add timer to vector and order by expiration time
    timers_storage.emplace_back(timer);
    std::sort(timers_storage.begin(), timers_storage.end(), less_than_key());

    // Clear heap as the pointers likely become invalid after the above emplace_back.
    heap.clear();
    for (auto& t : timers_storage) {
      heap.push_back(&t);
    }

    size = heap.size();
  }

  /**
   * @brief Get the time before the first timer in the heap expires
   *
   * @return std::chrono::nanoseconds to wait, the returned value could be negative if the timer
   * is already expired
   */
  inline std::chrono::nanoseconds get_head_timeout()
  {
    auto min_timeout = std::chrono::nanoseconds::max();
    TimerInternalPtr head;
    if (peek(&head) == 0) {
      min_timeout = (*head)->time_until_trigger();
    }

    return min_timeout;
  }

  /**
   * @brief Executes all the ready timers in the heap
   * After execution, timers are added back to the heap
   * NOTE: may block indefinitely if the time for processing callbacks is longer than the timers period
   */
  inline void execute_ready_timers()
  {
    TimerInternalPtr head;
    while (peek(&head) == 0 && (*head)->is_ready()) {
      (*head)->execute_callback();

      remove_at(0);
      push(head);
    }
  }

  inline void clear_all()
  {
    // Todo: Implement clear all timers.
  }

  inline void remove_timer(rclcpp::TimerBase::SharedPtr timer)
  {
    // Todo: Implement
    (void)timer;
  }

private:
/*
  struct TimerInternal
  {
    inline TimerInternal()
    {
      timer = nullptr;
    }

    inline TimerInternal(rclcpp::TimerBase::SharedPtr t)
    {
      timer = t;
    }

    bool operator < (const TimerInternal& t) const
    {
        return (time_until_trigger() < t.time_until_trigger());
    }

    rclcpp::TimerBase::SharedPtr timer;
  };
*/
  struct less_than_key
  {
    inline bool operator() (const rclcpp::TimerBase::SharedPtr& struct1, const rclcpp::TimerBase::SharedPtr& struct2)
    {
      return (struct1->time_until_trigger() < struct2->time_until_trigger());
    }
  };


  using TimerInternalPtr = rclcpp::TimerBase::SharedPtr*;

  inline void push(TimerInternalPtr x)
  {
    size_t i = size++;
    heap[i] = x;
    while (i && ((*x)->time_until_trigger() < (*heap[(i-1)/2])->time_until_trigger())) {
      heap[i] = heap[(i-1)/2];
      heap[(i-1)/2] = x;
      i = (i-1)/2;
    }
  }

  inline void remove_at(size_t i)
  {
    TimerInternalPtr y = heap[--size];
    heap[i] = y;

    // Heapify upwards.
    while (i > 0) {
      size_t parent = (i-1)/2;
      if ((*y)->time_until_trigger() < (*heap[parent])->time_until_trigger()) {
        heap[i] = heap[parent];
        heap[parent] = y;
        i = parent;
      } else {
        break;
      }
    }

    // Heapify downwards
    while (2*i + 1 < size) {
      size_t hi = i;
      size_t left = 2*i+1;
      size_t right = left + 1;
      if ((*y)->time_until_trigger() > (*heap[left])->time_until_trigger()) {
        hi = left;
      }
      if (right < size && ((*heap[hi])->time_until_trigger() > (*heap[right])->time_until_trigger())) {
        hi = right;
      }
      if (hi != i) {
        heap[i] = heap[hi];
        heap[hi] = y;
        i = hi;
      } else {
        break;
      }
    }
  }

  inline int pop(TimerInternalPtr x)
  {
    if (size == 0) {
      // The heap is empty, can't pop
      return -1;
    }

    x = heap[0];
    remove_at(0);
    return 0;
  }

  inline int peek(TimerInternalPtr* x)
  {
    if (size == 0) {
      // The heap is empty, can't peek
      return -1;
    }

    *x = heap[0];
    return 0;
  }

  inline int remove(TimerInternalPtr x)
  {
    size_t i;
    for (i = 0; i < size; ++i) {
      if (x == heap[i]) {
        break;
      }
    }
    if (i == size) {
      return -1;
    }

    remove_at(i);
    return 0;
  }

  // Vector to keep ownership of the timers
  std::vector<rclcpp::TimerBase::SharedPtr> timers_storage;
  // Vector of pointers to stored timers used to implement the priority queue
  std::vector<TimerInternalPtr> heap;
  // Current number of elements in the heap
  size_t size;
};

}
}

#endif  // RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_