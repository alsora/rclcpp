#include "rclcpp/executors/rclcpp_executor.hpp"
#include "rclcpp/experimental/subscription_intra_process_base.hpp"

#include <memory>

#include "rclcpp/scope_exit.hpp"

using rclcpp::executors::RclcppExecutor;
using rclcpp::executor::ExecutableList;

using namespace std::chrono_literals;

RclcppExecutor::RclcppExecutor(
  const rclcpp::executor::ExecutorArgs & args)
: executor::Executor(args)
{
  cv_ = std::make_shared<std::condition_variable>();
}

RclcppExecutor::~RclcppExecutor() {}

void
RclcppExecutor::spin()
{
  if (spinning.exchange(true)) {
    throw std::runtime_error("spin() called while already spinning");
  }
  RCLCPP_SCOPE_EXIT(this->spinning.store(false); );

  while (rclcpp::ok(this->context_) && spinning.load()) {
    execute_ready_executables();
  }
}

void
RclcppExecutor::add_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  // If the node already has an executor
  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();
  if (has_executor.exchange(true)) {
    throw std::runtime_error("Node has already been added to an executor.");
  }

  // Check in all the callback groups
  for (auto & weak_group : node_ptr->get_callback_groups()) {
    auto group = weak_group.lock();
    if (!group || !group->can_be_taken_from().load()) {
      continue;
    }
    group->find_timer_ptrs_if(
      [this](const rclcpp::TimerBase::SharedPtr & timer) {
        if (timer) {
          timers_.push_back(timer);
        }
        return false;
    });
    group->find_waitable_ptrs_if(
    [this](const rclcpp::Waitable::SharedPtr & waitable) {
      if (waitable) {
        auto ipc_waitable = std::dynamic_pointer_cast<experimental::SubscriptionIntraProcessBase>(waitable);
        if (ipc_waitable) {
          ipc_waitable->set_executor_cv(cv_);
          waitables_.push_back(waitable);
        }
      }
      return false;
    });
  }

  if (notify && spinning.load()) {
    // Interrupt waiting to handle new node
    cv_->notify_one();
  }
}

void
RclcppExecutor::add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->add_node(node_ptr->get_node_base_interface(), notify);
}

void
RclcppExecutor::remove_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr, bool notify)
{
  // TODO: this is not currently supported
  bool node_removed = false;

  if (notify && node_removed && spinning.load()) {
    cv_->notify_one();
  }

  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();
  has_executor.store(false);
}

void
RclcppExecutor::remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify)
{
  this->remove_node(node_ptr->get_node_base_interface(), notify);
}

void
RclcppExecutor::execute_ready_executables()
{
  // Calculate the timeout argument for the condition variable wait.
  bool is_timer_timeout = false;
  // By default no timeout.
  auto min_timeout = std::chrono::nanoseconds::max();
  for (auto timer : timers_) {
    auto timer_timeout = timer->time_until_trigger();
    if (timer_timeout < min_timeout) {
      is_timer_timeout = true;
      min_timeout = timer_timeout;
    }
  }

  // If min_timeout was negative, we need to wake up immediately.
  auto wait_timeout = (min_timeout < 0ns) ? 0ns : std::chrono::nanoseconds(min_timeout);

  std::unique_lock<std::mutex> lock = std::unique_lock<std::mutex>(m_);
  // No need to check a predicate here, as following actions are already checking it.
  std::cv_status wait_status = cv_->wait_for(lock, wait_timeout);

  // Check timers only if we woke up because timers expired.
  if (wait_status == std::cv_status::timeout && is_timer_timeout) {
    for (auto timer : timers_) {
      if (timer->is_ready()) {
        timer ->execute_callback();
      }
    }
  }

  // Always check waitables when waking up.
  for (auto waitable : waitables_) {
    if (waitable->is_ready(nullptr)) {
      waitable->execute();
    }
  }
}
