#ifndef RCLCPP__EXECUTORS__RCLCPP_EXECUTOR_HPP_
#define RCLCPP__EXECUTORS__RCLCPP_EXECUTOR_HPP_

#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <vector>
#include <string>

#include "rmw/rmw.h"

#include "rclcpp/executable_list.hpp"
#include "rclcpp/executor.hpp"
#include "rclcpp/executors/static_executor_entities_collector.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/memory_strategies.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/rate.hpp"
#include "rclcpp/utilities.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{
namespace executors
{

class RclcppExecutor : public executor::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(RclcppExecutor)

  /// Default constructor. See the default constructor for Executor.
  RCLCPP_PUBLIC
  explicit RclcppExecutor(
    const executor::ExecutorArgs & args = executor::ExecutorArgs());

  /// Default destrcutor.
  RCLCPP_PUBLIC
  virtual ~RclcppExecutor();

  /// Static executor implementation of spin.
  // This function will block until work comes in, execute it, and keep blocking.
  // It will only be interrupt by a CTRL-C (managed by the global signal handler).
  RCLCPP_PUBLIC
  void
  spin() override;

  /// Add a node to the executor.
  /**
   * An executor can have zero or more nodes which provide work during `spin` functions.
   * \param[in] node_ptr Shared pointer to the node to be added.
   * \param[in] notify True to trigger the interrupt guard condition during this function. If
   * the executor is blocked at the rmw layer while waiting for work and it is notified that a new
   * node was added, it will wake up.
   */
  RCLCPP_PUBLIC
  void
  add_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  RCLCPP_PUBLIC
  void
  add_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

  /// Remove a node from the executor.
  /**
   * \param[in] node_ptr Shared pointer to the node to remove.
   * \param[in] notify True to trigger the interrupt guard condition and wake up the executor.
   * This is useful if the last node was removed from the executor while the executor was blocked
   * waiting for work in another thread, because otherwise the executor would never be notified.
   */
  RCLCPP_PUBLIC
  void
  remove_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify = true) override;

  /// Convenience function which takes Node and forwards NodeBaseInterface.
  RCLCPP_PUBLIC
  void
  remove_node(std::shared_ptr<rclcpp::Node> node_ptr, bool notify = true) override;

protected:

  RCLCPP_PUBLIC
  void
  execute_ready_executables();

private:
  RCLCPP_DISABLE_COPY(RclcppExecutor)


  std::shared_ptr<std::condition_variable> cv_;
  std::mutex m_;

  // Vector containing the TimerBase of all the timers added to the executor.
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;

  // Vector containing all the Waitable added to the executor.
  std::vector<rclcpp::Waitable::SharedPtr> waitables_;
};

}  // namespace executors
}  // namespace rclcpp

#endif  // RCLCPP__EXECUTORS__RCLCPP_EXECUTOR_HPP_
