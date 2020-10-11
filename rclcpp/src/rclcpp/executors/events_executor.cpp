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

using namespace std::chrono_literals;

using rclcpp::executors::EventsExecutor;

constexpr std::chrono::nanoseconds rclcpp::executors::TimersManager::MAX_TIME;

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

  // When condition variable is notified, check this predicate to proceed
  auto predicate = [this]() { return !event_queue_.empty(); };

  // Local event queue
  std::queue<EventQ> local_event_queue;

  timers_manager_->start();

  while (rclcpp::ok(context_) && spinning.load()) {

    // Scope block for the mutex
    {
      std::unique_lock<std::mutex> lock(event_queue_mutex_);
      // We wait here until something has been pushed to the event queue
      event_queue_cv_.wait(lock, predicate);

      // We got an event! Swap queues and execute events
      std::swap(local_event_queue, event_queue_);
    }

    this->consume_all_events(local_event_queue);
  }

  timers_manager_->stop();
}

void
EventsExecutor::spin_some(std::chrono::nanoseconds max_duration)
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false););
  
  // This function will wait until the first of the following events occur:
  // - The input max_duration is elapsed
  // - A timer triggers
  // - An executor event is received and processed

  // Select the smallest between input max_duration and timer timeout
  auto next_timer_timeout = timers_manager_->get_head_timeout();
  if (next_timer_timeout < max_duration) {
    max_duration = next_timer_timeout;
  }

  std::queue<EventQ> local_event_queue;

  {
    // Wait until timeout or event
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    event_queue_cv_.wait_for(lock, max_duration);
    std::swap(local_event_queue, event_queue_);
  }

  timers_manager_->execute_ready_timers();
  this->consume_all_events(local_event_queue);
}

void
EventsExecutor::spin_all(std::chrono::nanoseconds max_duration)
{
  if (max_duration <= 0ns) {
    throw std::invalid_argument("max_duration must be positive");
  }

  if (spinning.exchange(true)) {
    throw std::runtime_error("spin_some() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false););

  std::queue<EventQ> local_event_queue;

  auto start = std::chrono::steady_clock::now();
  auto max_duration_not_elapsed = [max_duration, start]() {
      auto elapsed_time = std::chrono::steady_clock::now() - start;
      return elapsed_time < max_duration;
    };

  // Wait once
  // Select the smallest between input timeout and timer timeout
  auto next_timer_timeout = timers_manager_->get_head_timeout();
  if (next_timer_timeout < max_duration) {
    max_duration = next_timer_timeout;
  }

  {
    // Wait until timeout or event
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    event_queue_cv_.wait_for(lock, max_duration);
  }

  // Keep executing until work available or timeout expired
  while (rclcpp::ok(context_) && spinning.load() && max_duration_not_elapsed()) {

    {
      std::unique_lock<std::mutex> lock(event_queue_mutex_);
      std::swap(local_event_queue, event_queue_);
    }

    bool ready_timer = timers_manager_->get_head_timeout() < 0ns;
    bool has_events = !local_event_queue.empty();
    if (!ready_timer && !has_events) {
      // Exit as there is no more work to do
      break;
    }

    // Execute all ready work
    timers_manager_->execute_ready_timers();
    this->consume_all_events(local_event_queue);
  }
}

void
EventsExecutor::spin_once_impl(std::chrono::nanoseconds timeout)
{
  // In this context a negative input timeout means no timeout
  if (timeout < 0ns) {
    timeout = timers_manager_->MAX_TIME;
  }

  // Select the smallest between input timeout and timer timeout
  auto next_timer_timeout = timers_manager_->get_head_timeout();
  if (next_timer_timeout < timeout) {
    timeout = next_timer_timeout;
  }

  EventQ event;
  bool has_event = false;

  {
    // Wait until timeout or event arrives
    std::unique_lock<std::mutex> lock(event_queue_mutex_);
    event_queue_cv_.wait_for(lock, timeout);

    // Grab first event from queue if it exists
    has_event = !event_queue_.empty();
    if (has_event) {
      event = event_queue_.front();
      event_queue_.pop();
    }
  }

  // If we wake up from the wait with an event, it means that it
  // arrived before any of the timers expired.
  if (has_event) {
    this->execute_event(event);
  } else {
    timers_manager_->execute_head_timer();
  }
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

  this->consume_all_events(local_event_queue);
}

void
EventsExecutor::consume_all_events(std::queue<EventQ> &event_queue)
{
  while (!event_queue.empty()) {

    EventQ event = event_queue.front();
    event_queue.pop();

    this->execute_event(event);
  }
}

void
EventsExecutor::execute_event(const EventQ &event)
{
  switch(event.type) {
  case SUBSCRIPTION_EVENT:
    {
      auto subscription = const_cast<rclcpp::SubscriptionBase *>(
                    static_cast<const rclcpp::SubscriptionBase*>(event.entity));
      execute_subscription(subscription);
      break;
    }

  case SERVICE_EVENT:
    {
      auto service = const_cast<rclcpp::ServiceBase*>(
              static_cast<const rclcpp::ServiceBase*>(event.entity));
      execute_service(service);
      break;
    }

  case CLIENT_EVENT:
    {
      auto client = const_cast<rclcpp::ClientBase*>(
              static_cast<const rclcpp::ClientBase*>(event.entity));
      execute_client(client);
      break;
    }

  case GUARD_CONDITION_EVENT:
    {
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
