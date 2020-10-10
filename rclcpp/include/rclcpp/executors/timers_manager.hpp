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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
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
   * @brief Construct a new TimersManager object
   */
  TimersManager(std::shared_ptr<rclcpp::Context> context)
  {
    context_ = context;
  }

  /**
   * @brief Adds a new TimerBase to the storage.
   * This object will keep ownership of the timer.
   * @param timer the timer to be added
   */
  void add_timer(rclcpp::TimerBase::SharedPtr timer)
  {
    std::cout<<"adding timer: current size"<< timers_storage_.size()<<std::endl;

    {
      std::unique_lock<std::mutex> lock(timers_mutex_);

      // Make sure that the provided timer is not already in the timers storage
      if (std::find(timers_storage_.begin(), timers_storage_.end(), timer) != timers_storage_.end()) {
        std::cout<<"ignoring existing timer!!"<<std::endl;
        return;
      }

      // Store ownership of timer and add it to heap
      timers_storage_.emplace_back(timer);
      this->add_timer_to_heap(&(timers_storage_.back()));
    }

    // Notify that a timer has been added to the heap
    timers_cv_.notify_one();

    //verify();
  }

  /**
   * @brief Starts a thread that takes care of executing timers added to this object.
   */
  void start()
  {
    // Make sure that the thread is not already running
    if (running_) {
      throw std::runtime_error("TimersManager::start() can't start timers thread as already running");
    }

    timers_thread_ = std::thread(&TimersManager::run_timers, this);
    pthread_setname_np(timers_thread_.native_handle(), "TimersManager");
  }

  /**
   * @brief Stops the timers thread.
   */
  void stop()
  {
    std::cout<<"Stop"<<std::endl;
    // Notify stop to timers thread
    running_ = false;
    timers_cv_.notify_one(); 
    
    // Join timers thread if it's running
    if (timers_thread_.joinable()) {
      std::cout<<"Joining"<<std::endl;
      timers_thread_.join();
    }
    std::cout<<"Stop done"<<std::endl;
  }

  /**
   * @brief Executes all the ready timers while keeping the heap correctly sorted.
   * This function may block indefinitely if the time 
   * for processing callbacks is longer than the timers period.
   */
  void execute_ready_timers()
  {
    std::unique_lock<std::mutex> lock(timers_mutex_);
    this->execute_ready_timers_unsafe();
  }

  /**
   * @brief Get the amount of time before the next timer expires.
   *
   * @return std::chrono::nanoseconds to wait, 
   * the returned value could be negative if the timer is already expired
   * or nanoseconds::max if the heap is empty.
   */
  std::chrono::nanoseconds get_head_timeout()
  {
    std::unique_lock<std::mutex> lock(timers_mutex_);
    return this->get_head_timeout_unsafe();
  }

  /**
   * @brief Remove all the timers stored in the object.
   */
  void clear_all()
  {
    std::unique_lock<std::mutex> lock(timers_mutex_);
    std::cout<<"clear all!!"<<std::endl;
    heap_.clear();
    timers_storage_.clear();
  }

  /**
   * @brief Remove a single timer stored in the object.
   * @param timer the timer to remove.
   */
  void remove_timer(rclcpp::TimerBase::SharedPtr timer)
  {
    std::unique_lock<std::mutex> lock(timers_mutex_);
    std::cout<<"here should remove single timer"<<std::endl;

    // Make sure that we are currently storing the provided timer before proceeding
    auto it = std::find(timers_storage_.begin(), timers_storage_.end(), timer);
    if (it == timers_storage_.end()) {
      return;
    }

    // Remove timer from the storage and rebuild heap
    timers_storage_.erase(it);
    this->rebuild_heap();
  }

  /**
   * @brief Destruct the object making sure to stop thread and release memory.
   */
  ~TimersManager()
  {
    std::cout<<"Destructor"<<std::endl;
    // Make sure timers thread is stopped before destroying this object
    this->stop();

    this->clear_all();
  }  

private:
  RCLCPP_DISABLE_COPY(TimersManager)

  using TimerPtr = rclcpp::TimerBase::SharedPtr*;

  /**
   * @brief Rebuilds the heap queue from the timers storage
   * This function is meant to be called whenever something changes in the timers storage.
   * This function is not thread safe, you need to acquire a mutex before calling it.
   */
  void rebuild_heap()
  {
    heap_.clear();
    for (auto& t : timers_storage_) {
      this->add_timer_to_heap(&t);
    }
  }

  /**
   * @brief Get the amount of time before the next timer expires.
   * This function is not thread safe, acquire a mutex before calling it.
   *
   * @return std::chrono::nanoseconds to wait, 
   * the returned value could be negative if the timer is already expired
   * or nanoseconds::max if the heap is empty.
   */
  std::chrono::nanoseconds get_head_timeout_unsafe()
  {
    if (heap_.empty()) {
      return std::chrono::nanoseconds::max();
    }

    return (*heap_[0])->time_until_trigger();
  }

  /**
   * @brief Executes all the ready timers while keeping the heap correctly sorted.
   * This function may block indefinitely if the time 
   * for processing callbacks is longer than the timers period.
   * This function is not thread safe, acquire a mutex before calling it.
   */
  void execute_ready_timers_unsafe()
  {
    if (heap_.empty()) {
      return;
    }

    TimerPtr head = heap_.front();
    while ((*head)->is_ready()) {
      (*head)->execute_callback();
      this->restore_heap_up(0);
      //verify();
    }
  }

  /**
   * @brief Implements a loop that keeps executing ready timers.
   * This function is executed in the timers thread.
   */
  void run_timers()
  {
    running_ = true;
    while (rclcpp::ok(context_) && running_)
    {
      std::unique_lock<std::mutex> lock(timers_mutex_);
      auto time_to_sleep = this->get_head_timeout_unsafe();
      timers_cv_.wait_for(lock, time_to_sleep);
      this->execute_ready_timers_unsafe();
    }
  }

  /**
   * @brief Add a new timer to the heap and sort it.
   * @param x pointer to a timer owned by this object.
   */
  void add_timer_to_heap(TimerPtr x)
  {
    size_t i = heap_.size(); // Position where we are going to add timer
    heap_.push_back(x);
    while (i && ((*x)->time_until_trigger() < (*heap_[(i-1)/2])->time_until_trigger())) {
      heap_[i] = heap_[(i-1)/2];
      heap_[(i-1)/2] = x;
      i = (i-1)/2;
    }
  }

  /**
   * @brief Restore a valid heap after a value is increased.
   * @param i index of the updated value.
   */
  void restore_heap_up(size_t i)
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

  // Helper function to check the correctness of the heap.
  void verify()
  {
    for (size_t i = 0; i < heap_.size()/2; ++i) {
      size_t left = 2*i + 1;
      if (left < heap_.size()) {
        assert(((*heap_[left])->time_until_trigger().count() >= (*heap_[i])->time_until_trigger().count()));
      }
      size_t right = left + 1;
      if (right < heap_.size()) {
        assert(((*heap_[right])->time_until_trigger().count() >= (*heap_[i])->time_until_trigger().count()));
      }
    }
  }

  // Thread used to run the timers monitoring and execution
  std::thread timers_thread_;
  // Protects access to timers
  std::mutex timers_mutex_;
  // Notifies the timers thread whenever timers are added/removed
  std::condition_variable timers_cv_;
  // Indicates whether the timers thread is currently running or requested to stop
  std::atomic<bool> running_ {false};
  // Context of the parent executor
  std::shared_ptr<rclcpp::Context> context_;
  // Container to keep ownership of the timers
  std::list<rclcpp::TimerBase::SharedPtr> timers_storage_;
  // Vector of pointers to stored timers used to implement the priority queue
  std::vector<TimerPtr> heap_;
};

}
}

#endif  // RCLCPP__EXECUTORS__TIMERS_MANAGER_HPP_
