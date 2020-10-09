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
  TimersManager() = default;

  bool started = false;

  /**
   * @brief Adds a new TimerBase to the heap
   * @param timer the timer to be added
   */
  inline void add_timer(rclcpp::TimerBase::SharedPtr timer)
  {
    std::cout<<"adding timer"<<std::endl;

    if (started) {
      std::cout<<"ignoring"<<std::endl;
      return;
    }

    // Add timer to vector and order by expiration time
    timers_storage.emplace_back(timer);

    // Clear heap as the pointers likely become invalid after the above emplace_back.
    heap.clear();
    for (auto& t : timers_storage) {
      add_timer_to_heap(&t);
    }

    //verify();
  }

  /**
   * @brief Get the time before the first timer in the heap expires
   *
   * @return std::chrono::nanoseconds to wait, the returned value could be negative if the timer
   * is already expired
   */
  inline std::chrono::nanoseconds get_head_timeout()
  {
    if (heap.empty()) {
      return std::chrono::nanoseconds::max();
    }

    return (*heap[0])->time_until_trigger();
  }

  /**
   * @brief Executes all the ready timers in the heap
   * After execution, timers are added back to the heap
   * NOTE: may block indefinitely if the time for processing callbacks is longer than the timers period
   */
  inline void execute_ready_timers()
  {
    started = true;

    if (heap.empty()) {
      return;
    }

    TimerPtr head = heap.front();
    while ((*head)->is_ready()) {
      (*head)->execute_callback();
      restore_heap_up(0);
      verify();
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
  using TimerPtr = rclcpp::TimerBase::SharedPtr*;

  inline void add_timer_to_heap(TimerPtr x)
  {
    size_t i = heap.size(); // Position where we are going to add timer
    heap.push_back(x);
    while (i && ((*x)->time_until_trigger() < (*heap[(i-1)/2])->time_until_trigger())) {
      heap[i] = heap[(i-1)/2];
      heap[(i-1)/2] = x;
      i = (i-1)/2;
    }
  }

  inline void restore_heap_up(size_t i)
  {
    size_t start = i;
    TimerPtr updated_timer = heap[i];

    size_t left = 2*i + 1;
    while (left < heap.size()) {
      size_t right = left + 1;
      if (right < heap.size() && (*heap[left])->time_until_trigger() >= (*heap[right])->time_until_trigger()) {
        left = right;
      }
      heap[i] = heap[left];
      i = left;
      left = 2*i + 1;
    }

    while (i > start) {
      size_t parent = (i -1) >> 1;
      if ((*updated_timer)->time_until_trigger() < (*heap[parent])->time_until_trigger()) {
        heap[i] = heap[parent];
        i = parent;
        continue;
      }
      break;
    }

    heap[i] = updated_timer;
  }

/*
  inline void remove_at(size_t i)
  {
    TimerPtr y = heap[--size];
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
  */
  void verify()
  {
    //std::cout<<"verify"<<std::endl;
    for (size_t i = 0; i < heap.size()/2; ++i) {
      size_t left = 2*i + 1;
      if (left < heap.size()) {
        //std::cout<<"AA checking" << (*heap[left])->time_until_trigger().count() << " and "<< (*heap[i])->time_until_trigger().count() <<std::endl;
        assert(((*heap[left])->time_until_trigger().count() >= (*heap[i])->time_until_trigger().count()));
      }
      size_t right = left + 1;
      if (right < heap.size()) {
        //std::cout<<"BB checking" << (*heap[right])->time_until_trigger().count() << " and "<< (*heap[i])->time_until_trigger().count() <<std::endl;
        assert(((*heap[right])->time_until_trigger().count() >= (*heap[i])->time_until_trigger().count()));
      }
    }

    //std::cout<<"verified "<<std::endl;

  }

 /*
  inline int pop(TimerPtr x)
  {
    if (heap.size() == 0) {
      // The heap is empty, can't pop
      return -1;
    }

    x = heap[0];
    remove_at(0);
    return 0;
  }
  */

  /*
  inline int remove(TimerPtr x)
  {
    size_t i;
    for (i = 0; i < heap.size(); ++i) {
      if (x == heap[i]) {
        break;
      }
    }
    if (i == heap.size()) {
      return -1;
    }

    remove_at(i);
    return 0;
  }
  */
  // Vector to keep ownership of the timers
  std::vector<rclcpp::TimerBase::SharedPtr> timers_storage;
  // Vector of pointers to stored timers used to implement the priority queue
  std::vector<TimerPtr> heap;
  // Current number of elements in the heap
  //size_t size;
};

}
}

#endif  // RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_
