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

#ifndef RCLCPP__INTRA_PROCESS_MANAGER_HPP_
#define RCLCPP__INTRA_PROCESS_MANAGER_HPP_

#include <rmw/types.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <set>

#include "rclcpp/allocator/allocator_deleter.hpp"
#include "rclcpp/intra_process_manager_impl.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{

// Forward declarations
class SubscriptionIntraProcessBase;
class PublisherBase;

namespace intra_process_manager
{

/// This class performs intra process communication between nodes.
/**
 * This class is used in the creation of publishers and subscriptions.
 * A singleton instance of this class is owned by a rclcpp::Context and a
 * rclcpp::Node can use an associated Context to get an instance of this class.
 * Nodes which do not have a common Context will not exchange intra process
 * messages because they will not share access to an instance of this class.
 *
 * When a Node creates a subscription, it can also create an additional
 * wrapper meant to receive intra process messages.
 * This structure can be registered with this class.
 * It is also allocated an id which is unique among all publishers
 * and subscriptions in this process and that is associated to the subscription.
 *
 * When a Node creates a publisher, as before this can be registered with this class.
 * This is required in order to publish messages intra-process.
 * It is also allocated an id which is unique among all publishers
 * and subscriptions in this process and that is associated to the publisher.
 *
 * When a publisher or a subscription are registered with this class, an internal
 * structure is updated in order to store which of them can communicate.
 * i.e. they have the same topic and compatible QoS.
 *
 * When the user publishes a message, if intra-process communication is enabled
 * on the publisher, the message is handed to this class.
 * Using the publisher id, a list of recipients for the message is selected.
 * For each item in the list, this class stores its intra-process wrapper.
 *
 * The wrapper contains a buffer where published intra-process messages are stored
 * until the subscription picks them up.
 * Depending on the data type stored in the buffer, the subscription intra process
 * can request ownership on the inserted messages.
 *
 * Thus, when an intra-process message is published, this class knows how many
 * intra-process subscriptions needs it and how many require ownership.
 * This information allows to efficiently perform a minimum number of copies of the message.
 *
 * This class is neither CopyConstructable nor CopyAssignable.
 */
class IntraProcessManager
{
private:
  RCLCPP_DISABLE_COPY(IntraProcessManager)

public:
  RCLCPP_SMART_PTR_DEFINITIONS(IntraProcessManager)

  RCLCPP_PUBLIC
  explicit IntraProcessManager(
    IntraProcessManagerImplBase::SharedPtr state = create_default_impl());

  RCLCPP_PUBLIC
  virtual ~IntraProcessManager();

  /// Register a subscription with the manager, returns subscriptions unique id.
  /**
   * This method stores the subscription intra process object, together with
   * the information of its wrapped subscription (i.e. topic name and QoS).
   *
   * In addition this generates a unique intra process id for the subscription.
   *
   * \param subscription the SubscriptionIntraProcess to register.
   * \return an unsigned 64-bit integer which is the subscription's unique id.
   */
  RCLCPP_PUBLIC
  uint64_t
  add_subscription(
    SubscriptionIntraProcessBase::SharedPtr subscription);

  /// Unregister a subscription using the subscription's unique id.
  /**
   * This method does not allocate memory.
   *
   * \param intra_process_subscription_id id of the subscription to remove.
   */
  RCLCPP_PUBLIC
  void
  remove_subscription(uint64_t intra_process_subscription_id);

  /// Register a publisher with the manager, returns the publisher unique id.
  /**
   * This method stores the publisher intra process object, together with
   * the information of its wrapped publisher (i.e. topic name and QoS).
   *
   * In addition this generates a unique intra process id for the publisher.
   *
   * \param publisher publisher to be registered with the manager.
   * \return an unsigned 64-bit integer which is the publisher's unique id.
   */
  RCLCPP_PUBLIC
  uint64_t
  add_publisher(
    PublisherBase::SharedPtr publisher);

  /// Unregister a publisher using the publisher's unique id.
  /**
   * This method does not allocate memory.
   *
   * \param intra_process_publisher_id id of the publisher to remove.
   */
  RCLCPP_PUBLIC
  void
  remove_publisher(uint64_t intra_process_publisher_id);

  /// Publishes an intra-process message, passed as a shared pointer.
  /**
   * This is one of the two methods for publishing intra-process.
   *
   * Using the intra-process publisher id, a list of recipients is obtained.
   * This list is split in half, depending whether they require ownership or not.
   *
   * This particular method takes a shared pointer as input.
   * This can be safely passed to all the subscriptions that do not require ownership.
   * No copies are needed in this case.
   * For every subscription requiring ownership, a copy has to be made.
   *
   * The total number of copies is always equal to the number
   * of subscriptions requiring ownership.
   *
   * This method can throw an exception if the publisher id is not found or
   * if the publisher shared_ptr given to add_publisher has gone out of scope.
   *
   * This method does allocate memory.
   *
   * \param intra_process_publisher_id the id of the publisher of this message.
   * \param message the message that is being stored.
   */
  template<typename MessageT>
  void
  do_intra_process_publish(
    uint64_t intra_process_publisher_id,
    std::shared_ptr<const MessageT> message)
  {
    std::set<uint64_t> take_shared_subscription_ids;
    std::set<uint64_t> take_owned_subscription_ids;

    impl_->get_subscription_ids_for_pub(
      take_shared_subscription_ids,
      take_owned_subscription_ids,
      intra_process_publisher_id);

    this->template add_shared_msg_to_buffers<MessageT>(message, take_shared_subscription_ids);

    if (take_owned_subscription_ids.size() > 0) {
      auto unique_msg = std::make_unique<MessageT>(*message);
      this->template add_owned_msg_to_buffers<MessageT>(
        std::move(unique_msg),
        take_owned_subscription_ids);
    }
  }

  /// Publishes an intra-process message, passed as a unique pointer.
  /**
   * This is one of the two methods for publishing intra-process.
   *
   * Using the intra-process publisher id, a list of recipients is obtained.
   * This list is split in half, depending whether they require ownership or not.
   *
   * This particular method takes a unique pointer as input.
   * The pointer can be promoted to a shared pointer and passed to all the subscriptions
   * that do not require ownership.
   * In case of subscriptions requiring ownership, the message will be copied for all of
   * them except the last one, when ownership can be transferred.
   *
   * This method can save an additional copy compared to the shared pointer one.
   *
   * This method can throw an exception if the publisher id is not found or
   * if the publisher shared_ptr given to add_publisher has gone out of scope.
   *
   * This method does allocate memory.
   *
   * \param intra_process_publisher_id the id of the publisher of this message.
   * \param message the message that is being stored.
   */
  template<
    typename MessageT,
    typename Deleter = std::default_delete<MessageT>>
  void
  do_intra_process_publish(
    uint64_t intra_process_publisher_id,
    std::unique_ptr<MessageT, Deleter> message)
  {
    std::set<uint64_t> take_shared_subscription_ids;
    std::set<uint64_t> take_owned_subscription_ids;

    impl_->get_subscription_ids_for_pub(
      take_shared_subscription_ids,
      take_owned_subscription_ids,
      intra_process_publisher_id);

    if (take_owned_subscription_ids.size() == 0) {
      std::shared_ptr<MessageT> msg = std::move(message);

      this->template add_shared_msg_to_buffers<MessageT>(msg, take_shared_subscription_ids);
    } else if (take_owned_subscription_ids.size() > 0 && take_shared_subscription_ids.size() <= 1) {
      // merge the two vector of ids into a unique one
      take_owned_subscription_ids.insert(
        take_shared_subscription_ids.begin(), take_shared_subscription_ids.end());

      this->template add_owned_msg_to_buffers<MessageT, Deleter>(
        std::move(message),
        take_owned_subscription_ids);
    } else if (take_owned_subscription_ids.size() > 0 && take_shared_subscription_ids.size() > 1) {
      std::shared_ptr<MessageT> shared_msg = std::make_shared<MessageT>(*message);

      this->template add_shared_msg_to_buffers<MessageT>(shared_msg, take_shared_subscription_ids);
      this->template add_owned_msg_to_buffers<MessageT, Deleter>(
        std::move(message),
        take_owned_subscription_ids);
    }
  }

  /// Return true if the given rmw_gid_t matches any stored Publishers.
  RCLCPP_PUBLIC
  bool
  matches_any_publishers(const rmw_gid_t * id) const;

  /// Return the number of intraprocess subscriptions that are matched with a given publisher id.
  RCLCPP_PUBLIC
  size_t
  get_subscription_count(uint64_t intra_process_publisher_id) const;

private:
  RCLCPP_PUBLIC
  static uint64_t
  get_next_unique_id();

  template<typename MessageT>
  void
  add_shared_msg_to_buffers(
    std::shared_ptr<const MessageT> message,
    std::set<uint64_t> subscription_ids)
  {
    for (auto it = subscription_ids.begin(); it != subscription_ids.end(); it++) {
      auto subscription_base = impl_->get_subscription(*it);
      if (subscription_base == nullptr) {
        throw std::runtime_error("subscription has unexpectedly gone out of scope");
      }

      auto subscription =
        std::static_pointer_cast<SubscriptionIntraProcess<MessageT>>(subscription_base);

      subscription->provide_intra_process_message(message);
    }
  }

  template<
    typename MessageT,
    typename Deleter = std::default_delete<MessageT>>
  void
  add_owned_msg_to_buffers(
    std::unique_ptr<MessageT, Deleter> message,
    std::set<uint64_t> subscription_ids)
  {
    for (auto it = subscription_ids.begin(); it != subscription_ids.end(); it++) {
      auto subscription_base = impl_->get_subscription(*it);
      if (subscription_base == nullptr) {
        throw std::runtime_error("subscription has unexpectedly gone out of scope");
      }

      auto subscription =
        std::static_pointer_cast<SubscriptionIntraProcess<MessageT>>(subscription_base);

      if (std::next(it) == subscription_ids.end()) {
        // If this is the last subscription, give up ownership
        subscription->provide_intra_process_message(std::move(message));
      } else {
        // Copy the message since we have additional subscriptions to serve
        std::unique_ptr<MessageT, Deleter> copy_message = std::make_unique<MessageT>(*message);
        subscription->provide_intra_process_message(std::move(copy_message));
      }
    }
  }

  IntraProcessManagerImplBase::SharedPtr impl_;
};

}  // namespace intra_process_manager
}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_MANAGER_HPP_
