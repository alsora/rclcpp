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

#include "rclcpp/executors/events_executor.hpp"

using rclcpp::executors::EventsExecutor;

EventsExecutor::EventsExecutor(
  const rclcpp::ExecutorOptions & options)
: rclcpp::Executor(options)
{
  timers_manager_ = std::make_shared<TimersManager>(context_);
  entities_collector_ = std::make_shared<EventsExecutorEntitiesCollector>();

  // Set entities collector callbacks
  entities_collector_->init(
    this,
    &EventsExecutor::push_event,
    [this](const rclcpp::TimerBase::SharedPtr & t) {
      timers_manager_->add_timer(t);
    },
    [this](const rclcpp::TimerBase::SharedPtr & t) {
      timers_manager_->remove_timer(t);
    },
    [this]() {
      timers_manager_->clear_all();
    });

  rcl_ret_t ret;

  // Set the global ctrl-c guard condition callback
  ret = rcl_guard_condition_set_callback(
    this,
    &EventsExecutor::push_event,
    entities_collector_.get(),
    options.context->get_interrupt_guard_condition(&wait_set_),
    false /* Discard previous events */);
  
  if (ret != RCL_RET_OK) {
    throw std::runtime_error("Couldn't set ctrl-c guard condition callback");
  }

  // Set the executor interrupt guard condition callback
  ret = rcl_guard_condition_set_callback(
    this,
    &EventsExecutor::push_event,
    entities_collector_.get(),
    &interrupt_guard_condition_,
    false /* Discard previous events */);

  if (ret != RCL_RET_OK) {
    throw std::runtime_error("Couldn't set interrupt guard condition callback");
  }
}

EventsExecutor::~EventsExecutor() {}

void
EventsExecutor::spin()
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false););

  timers_manager_->start();

  while (rclcpp::ok(context_) && spinning.load()) {
    this->handle_events();
  }

  timers_manager_->stop();
}

void
EventsExecutor::spin_some(std::chrono::nanoseconds max_duration)
{
  // Check, are we already spinning?
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false););
  
  /**
  // Execute ready timers and ready events without any wait
  if (rclcpp::ok(context_)) {
    timers_manager_->execute_ready_timers();
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    this->execute_events(event_queue_);
  }
  */

  this->spin_once_impl(max_duration);
}

void
EventsExecutor::spin_all(std::chrono::nanoseconds max_duration)
{
  (void)max_duration;

  // Check, are we already spinning?
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false););

  this->spin_once_impl(max_duration);
}

void
EventsExecutor::spin_once_impl(std::chrono::nanoseconds timeout)
{
  // This function will wait until the first of the following events occur:
  // - The input timeout is elapsed
  // - A timer triggers
  // - An executor event is received and processed

  std::cout<<"spin once impl"<<std::endl;
  auto original = timeout;
  // Select the smallest between input timeout and timer timeout
  auto next_timer_timeout = timers_manager_->get_head_timeout();
  if (next_timer_timeout < timeout) {
    timeout = next_timer_timeout;
  }

  std::cout<<"Selecting timeout "<< timeout.count()<< " between timers "<< next_timer_timeout.count() <<" and original "<< original.count()<<std::endl;
  auto start_time = std::chrono::steady_clock::now();

  std::queue<EventQ> local_event_queue;
  std::cv_status wait_status;

  // Scope block for the mutex
  {
    // Wait until timeout or event
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    wait_status = event_queue_cv_.wait_for(lock, timeout);

    std::cout<<"spin once impl woke up after "<< (std::chrono::steady_clock::now() - start_time).count()<<std::endl;

    std::swap(local_event_queue, event_queue_);
  }

  if (wait_status == std::cv_status::timeout) {
    std::cout<<"this was a timeout --> execute timers"<<std::endl;
    timers_manager_->execute_ready_timers();
  }

  std::cout<<"executing events"<<std::endl;
  this->execute_events(local_event_queue);
  std::cout<<"spin once impl DONE"<<std::endl;
}

void
EventsExecutor::add_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  (void) notify;

  // Add node to entities collector
  entities_collector_->add_node(node_ptr);

  // Get nodes entities, and assign their callbaks
  for (auto & weak_group : node_ptr->get_callback_groups()) {
    auto group = weak_group.lock();
    if (!group || !group->can_be_taken_from().load()) {
      continue;
    }
    // Add timers to timers to timer manager
    group->find_timer_ptrs_if(
      [this](const rclcpp::TimerBase::SharedPtr & timer) {
        if (timer) {
          timers_manager_->add_timer(timer);
        }
        return false;
    });
    // Set the callbacks to all the entities
    group->find_subscription_ptrs_if(
      [this](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
        if (subscription) {
          subscription->set_callback(this, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_service_ptrs_if(
      [this](const rclcpp::ServiceBase::SharedPtr & service) {
        if (service) {
          service->set_callback(this, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_client_ptrs_if(
      [this](const rclcpp::ClientBase::SharedPtr & client) {
        if (client) {
          client->set_callback(this, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_waitable_ptrs_if(
      [this](const rclcpp::Waitable::SharedPtr & waitable) {
        if (waitable) {
          waitable->set_guard_condition_callback(this, &EventsExecutor::push_event);
        }
        return false;
      });
  }

  // Set node's guard condition callback, so if new entities are added while
  // spinning we can set their callback.
  rcl_ret_t ret = rcl_guard_condition_set_callback(
                    this,
                    &EventsExecutor::push_event,
                    entities_collector_.get(),
                    node_ptr->get_notify_guard_condition(),
                    false /* Discard previous events */);

  if (ret != RCL_RET_OK) {
    throw std::runtime_error("Couldn't set node guard condition callback");
  }
}

void
EventsExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->add_node(node_ptr->get_node_base_interface(), notify);
}

void
EventsExecutor::remove_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  (void)notify;
  entities_collector_->remove_node(node_ptr);

  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();
  has_executor.store(false);
}

void
EventsExecutor::remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->remove_node(node_ptr->get_node_base_interface(), notify);
}

void
EventsExecutor::handle_events()
{
  // When condition variable is notified, check this predicate to proceed
  auto predicate = [this]() { return !event_queue_.empty(); };

  // Local event queue
  std::queue<EventQ> local_event_queue;

  // Scope block for the mutex
  {
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    // We wait here until something has been pushed to the event queue
    event_queue_cv_.wait(lock, predicate);

    // We got an event! Swap queues and execute events
    std::swap(local_event_queue, event_queue_);
  }

  this->execute_events(local_event_queue);
}

void
EventsExecutor::execute_events(std::queue<EventQ> &event_queue)
{
  while (!event_queue.empty()) {

    EventQ event = event_queue.front();
    event_queue.pop();
    std::cout<<"poppin event"<<std::endl;
    switch(event.type) {
    case SUBSCRIPTION_EVENT:
      {
        std::cout<<"subscription event"<<std::endl;
        auto subscription = const_cast<rclcpp::SubscriptionBase *>(
                     static_cast<const rclcpp::SubscriptionBase*>(event.entity));
        execute_subscription(subscription);
        break;
      }

    case SERVICE_EVENT:
      {
        std::cout<<"service event"<<std::endl;
        auto service = const_cast<rclcpp::ServiceBase*>(
                static_cast<const rclcpp::ServiceBase*>(event.entity));
        execute_service(service);
        break;
      }

    case CLIENT_EVENT:
      {
        std::cout<<"client event"<<std::endl;
        auto client = const_cast<rclcpp::ClientBase*>(
               static_cast<const rclcpp::ClientBase*>(event.entity));
        execute_client(client);
        break;
      }

    case GUARD_CONDITION_EVENT:
      {
        std::cout<<"waitable event"<<std::endl;
        auto waitable = const_cast<rclcpp::Waitable*>(
                 static_cast<const rclcpp::Waitable*>(event.entity));
        waitable->execute();
        break;
      }

    default:
      throw std::runtime_error("EventsExecutor received unrecognized event");
      break;
    }  
  }
}
