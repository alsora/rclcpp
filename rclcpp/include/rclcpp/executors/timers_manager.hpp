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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include <rclcpp/context.hpp>
#include <rclcpp/timer.hpp>

namespace rclcpp
{
namespace executors
{

class TimersManager
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(TimersManager)

  /**
   * @brief Construct a new Timers heap_ object
   */
  TimersManager(std::shared_ptr<rclcpp::Context> context)
  {
    context_ = context;
  }

  bool started = false;

  /**
   * @brief Adds a new TimerBase to the heap_
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
    timers_storage_.emplace_back(timer);

    // Rebuild heap_ as pointers to elements in timers_storage_ vector become invalid after resize
    heap_.clear();
    for (auto& t : timers_storage_) {
      add_timer_to_heap(&t);
    }

    //verify();
  }

  void run_timers()
  {
    running_ = true;
    while (rclcpp::ok(context_) && running_)
    {
      std::unique_lock<std::mutex> lock(timers_heap_mutex_);
      auto time_to_sleep = this->get_head_timeout();
      timers_heap_cv.wait_for(lock, time_to_sleep);
      this->execute_ready_timers();
    }
  }

  /*
  void start()
  {
    t_spin_timers = std::thread(&TimersManager::run_timers, this);
    pthread_setname_np(t_spin_timers.native_handle(), "Timers");
  }
  */

   /*
  void run_timers()
  {
    running_ = true;
    while (rclcpp::ok() && running_)
    {
      std::cout<<"Run"<<std::endl;
      std::unique_lock<std::mutex> lock(timers_heap_mutex_);
      auto time_to_sleep = this->get_head_timeout();
      timers_heap_cv.wait_for(lock, time_to_sleep);
      //std::this_thread::sleep_for(time_to_sleep);
      this->execute_ready_timers();
    }
    std::cout<<"DONE"<<std::endl;
  }


  void stop()
  {
    std::cout<<"Stop"<<std::endl;
    // notify stop
    running_ = false;
    timers_heap_cv.notify_one(); 
    
    if (t_spin_timers.joinable()) {
      std::cout<<"Joining"<<std::endl;
      t_spin_timers.join();
    }
    std::cout<<"Stop done"<<std::endl;
  }

  ~TimersManager()
  {
    std::cout<<"Destructor"<<std::endl;
    this->stop();

    timers_storage_.clear();
    heap_.clear();
  }  
  */
  /**
   * @brief Get the time before the first timer in the heap_ expires
   *
   * @return std::chrono::nanoseconds to wait, the returned value could be negative if the timer
   * is already expired
   */
  inline std::chrono::nanoseconds get_head_timeout()
  {
    if (heap_.empty()) {
      return std::chrono::nanoseconds::max();
    }

    return (*heap_[0])->time_until_trigger();
  }

  /**
   * @brief Executes all the ready timers in the heap_
   * After execution, timers are added back to the heap_
   * NOTE: may block indefinitely if the time for processing callbacks is longer than the timers period
   */
  inline void execute_ready_timers()
  {
    started = true;

    if (heap_.empty()) {
      return;
    }

    TimerPtr head = heap_.front();
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
    size_t i = heap_.size(); // Position where we are going to add timer
    heap_.push_back(x);
    while (i && ((*x)->time_until_trigger() < (*heap_[(i-1)/2])->time_until_trigger())) {
      heap_[i] = heap_[(i-1)/2];
      heap_[(i-1)/2] = x;
      i = (i-1)/2;
    }
  }

  inline void restore_heap_up(size_t i)
  {
    size_t start = i;
    TimerPtr updated_timer = heap_[i];

    size_t left = 2*i + 1;
    while (left < heap_.size()) {
      size_t right = left + 1;
      if (right < heap_.size() && (*heap_[left])->time_until_trigger() >= (*heap_[right])->time_until_trigger()) {
        left = right;
      }
      heap_[i] = heap_[left];
      i = left;
      left = 2*i + 1;
    }

    while (i > start) {
      size_t parent = (i -1) >> 1;
      if ((*updated_timer)->time_until_trigger() < (*heap_[parent])->time_until_trigger()) {
        heap_[i] = heap_[parent];
        i = parent;
        continue;
      }
      break;
    }

    heap_[i] = updated_timer;
  }

/*
  inline void remove_at(size_t i)
  {
    TimerPtr y = heap_[--size];
    heap_[i] = y;

    // Heapify upwards.
    while (i > 0) {
      size_t parent = (i-1)/2;
      if ((*y)->time_until_trigger() < (*heap_[parent])->time_until_trigger()) {
        heap_[i] = heap_[parent];
        heap_[parent] = y;
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
      if ((*y)->time_until_trigger() > (*heap_[left])->time_until_trigger()) {
        hi = left;
      }
      if (right < size && ((*heap_[hi])->time_until_trigger() > (*heap_[right])->time_until_trigger())) {
        hi = right;
      }
      if (hi != i) {
        heap_[i] = heap_[hi];
        heap_[hi] = y;
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
    for (size_t i = 0; i < heap_.size()/2; ++i) {
      size_t left = 2*i + 1;
      if (left < heap_.size()) {
        //std::cout<<"AA checking" << (*heap_[left])->time_until_trigger().count() << " and "<< (*heap_[i])->time_until_trigger().count() <<std::endl;
        assert(((*heap_[left])->time_until_trigger().count() >= (*heap_[i])->time_until_trigger().count()));
      }
      size_t right = left + 1;
      if (right < heap_.size()) {
        //std::cout<<"BB checking" << (*heap_[right])->time_until_trigger().count() << " and "<< (*heap_[i])->time_until_trigger().count() <<std::endl;
        assert(((*heap_[right])->time_until_trigger().count() >= (*heap_[i])->time_until_trigger().count()));
      }
    }

    //std::cout<<"verified "<<std::endl;

  }

 /*
  inline int pop(TimerPtr x)
  {
    if (heap_.size() == 0) {
      // The heap_ is empty, can't pop
      return -1;
    }

    x = heap_[0];
    remove_at(0);
    return 0;
  }
  */

  /*
  inline int remove(TimerPtr x)
  {
    size_t i;
    for (i = 0; i < heap_.size(); ++i) {
      if (x == heap_[i]) {
        break;
      }
    }
    if (i == heap_.size()) {
      return -1;
    }

    remove_at(i);
    return 0;
  }
  */


  //std::thread t_spin_timers;

  std::mutex timers_heap_mutex_;
  std::condition_variable timers_heap_cv;

  std::atomic<bool> running_ {false};

  std::shared_ptr<rclcpp::Context> context_;
  // Vector to keep ownership of the timers
  std::vector<rclcpp::TimerBase::SharedPtr> timers_storage_;
  // Vector of pointers to stored timers used to implement the priority queue
  std::vector<TimerPtr> heap_;
};

}
}

#endif  // RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_
