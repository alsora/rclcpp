// Copyright 2015 Open Source Robotics Foundation, Inc.
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
#include "rclcpp/publisher_base.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/subscription_intra_process.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{
namespace intra_process_manager
{

/// This class facilitates intra process communication between nodes.
/**
 * This class is used in the creation of publishers and subscriptions.
 * A singleton instance of this class is owned by a rclcpp::Context and a
 * rclcpp::Node can use an associated Context to get an instance of this class.
 * Nodes which do not have a common Context will not exchange intra process
 * messages because they will not share access to an instance of this class.
 *
 * When a Node creates a publisher or subscription, it will register them
 * with this class.
 * The node will also hook into the publisher's publish call
 * in order to do intra process related work.
 *
 * When a publisher is created, it advertises on the topic the user provided,
 * as well as a "shadowing" topic of type rcl_interfaces/IntraProcessMessage.
 * For instance, if the user specified the topic '/namespace/chatter', then the
 * corresponding intra process topic might be '/namespace/chatter/_intra'.
 * The publisher is also allocated an id which is unique among all publishers
 * and subscriptions in this process.
 * Additionally, when registered with this class a ring buffer is created and
 * owned by this class as a temporary place to hold messages destined for intra
 * process subscriptions.
 *
 * When a subscription is created, it subscribes to the topic provided by the
 * user as well as to the corresponding intra process topic.
 * It is also gets a unique id from the singleton instance of this class which
 * is unique among publishers and subscriptions.
 *
 * When the user publishes a message, the message is stored by calling
 * store_intra_process_message on this class.
 * The instance of that message is uniquely identified by a publisher id and a
 * message sequence number.
 * The publisher id, message sequence pair is unique with in the process.
 * At that point a list of the id's of intra process subscriptions which have
 * been registered with the singleton instance of this class are stored with
 * the message instance so that delivery is only made to those subscriptions.
 * Then an instance of rcl_interfaces/IntraProcessMessage is published to the
 * intra process topic which is specific to the topic specified by the user.
 *
 * When an instance of rcl_interfaces/IntraProcessMessage is received by a
 * subscription, then it is handled by calling take_intra_process_message
 * on a singleton of this class.
 * The subscription passes a publisher id, message sequence pair which
 * uniquely identifies the message instance it was suppose to receive as well
 * as the subscriptions unique id.
 * If the message is still being held by this class and the subscription's id
 * is in the list of intended subscriptions then the message is returned.
 * If either of those predicates are not satisfied then the message is not
 * returned and the subscription does not call the users callback.
 *
 * Since the publisher builds a list of destined subscriptions on publish, and
 * other requests are ignored, this class knows how many times a message
 * instance should be requested.
 * The final time a message is requested, the ownership is passed out of this
 * class and passed to the final subscription, effectively freeing space in
 * this class's internal storage.
 *
 * Since a topic is being used to ferry notifications about new intra process
 * messages between publishers and subscriptions, it is possible for that
 * notification to be lost.
 * It is also possible that a subscription which was available when publish was
 * called will no longer exist once the notification gets posted.
 * In both cases this might result in a message instance getting requested
 * fewer times than expected.
 * This is why the internal storage of this class is a ring buffer.
 * That way if a message is orphaned it will eventually be dropped from storage
 * when a new message instance is stored and will not result in a memory leak.
 *
 * However, since the storage system is finite, this also means that a message
 * instance might get displaced by an incoming message instance before all
 * interested parties have called take_intra_process_message.
 * Because of this the size of the internal storage should be carefully
 * considered.
 *
 * /TODO(wjwwood): update to include information about handling latching.
 * /TODO(wjwwood): consider thread safety of the class.
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
   * In addition to generating a unique intra process id for the subscription,
   * this method also stores the topic name of the subscription.
   *
   * This method is normally called during the creation of a subscription,
   * but after it creates the internal intra process rmw_subscription_t.
   *
   * This method will allocate memory.
   *
   * \param subscription the Subscription to register.
   * \return an unsigned 64-bit integer which is the subscription's unique id.
   */
  RCLCPP_PUBLIC
  uint64_t
  add_subscription(
    SubscriptionBase::SharedPtr subscription);

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
   * In addition to generating and returning a unique id for the publisher,
   * this method creates internal ring buffer storage for "in-flight" intra
   * process messages which are stored when store_intra_process_message is
   * called with this publisher's unique id.
   *
   * The buffer_size must be less than or equal to the max uint64_t value.
   * If the buffer_size is 0 then a buffer size is calculated using the
   * publisher's QoS settings.
   * The default is to use the depth field of the publisher's QoS.
   * TODO(wjwwood): Consider doing depth *= 1.2, round up, or similar.
   * TODO(wjwwood): Consider what to do for keep all.
   *
   * This method is templated on the publisher's message type so that internal
   * storage of the same type can be allocated.
   *
   * This method will allocate memory.
   *
   * \param publisher publisher to be registered with the manager.
   * \param buffer_size if 0 (default) a size is calculated based on the QoS.
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

  /// Store a message in the manager, and return the message sequence number.
  /**
   * The given message is stored in internal storage using the given publisher
   * id and the newly generated message sequence, which is also returned.
   * The combination of publisher id and message sequence number can later
   * be used with a subscription id to retrieve the message by calling
   * take_intra_process_message.
   * The number of times take_intra_process_message can be called with this
   * unique pair of id's is determined by the number of subscriptions currently
   * subscribed to the same topic and which share the same Context, i.e. once
   * for each subscription which should receive the intra process message.
   *
   * The ownership of the incoming message is transfered to the internal
   * storage in order to avoid copying the message data.
   * Therefore, the message parameter will no longer contain the original
   * message after calling this method.
   * Instead it will either be a nullptr or it will contain the ownership of
   * the message instance which was displaced.
   * If the message parameter is not equal to nullptr after calling this method
   * then a message was prematurely displaced, i.e. take_intra_process_message
   * had not been called on it as many times as was expected.
   *
   * This method can throw an exception if the publisher id is not found or
   * if the publisher shared_ptr given to add_publisher has gone out of scope.
   *
   * This method does allocate memory.
   *
   * \param intra_process_publisher_id the id of the publisher of this message.
   * \param message the message that is being stored.
   * \return the message sequence number.
   */
  template<typename MessageT>
  void
  do_intra_process_publish(
    uint64_t intra_process_publisher_id,
    std::shared_ptr<const MessageT> message)
  {
    // get the publisher object in order to check its durability QoS
    auto weak_publisher = impl_->get_publisher(intra_process_publisher_id);
    auto publisher = weak_publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    if (publisher->get_actual_qos().durability != RMW_QOS_POLICY_DURABILITY_VOLATILE) {
      using IntraProcessBufferT = PublisherIntraProcessBuffer<MessageT>;

      auto buffer_base = publisher->get_intra_process_buffer();
      auto buffer = std::static_pointer_cast<IntraProcessBufferT>(buffer_base);

      buffer->add(message);
    }

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

  template<typename MessageT>
  void
  get_transient_local_messages(
    uint64_t intra_process_subscription_id)
  {
    using PublisherBufferT = PublisherIntraProcessBuffer<MessageT>;
    using IntraProcessBufferT = typename intra_process_buffer::IntraProcessBuffer<MessageT>;

    auto publishers_set = impl_->get_all_matching_publishers(intra_process_subscription_id);

    auto weak_subscription = impl_->get_subscription(intra_process_subscription_id);
    auto subscription = weak_subscription.lock();
    if (!subscription) {
      throw std::runtime_error("subscription has unexpectedly gone out of scope");
    }

    auto sub_buffer_base = subscription->get_intra_process_buffer();
    std::shared_ptr<IntraProcessBufferT> sub_buffer =
      std::static_pointer_cast<IntraProcessBufferT>(sub_buffer_base);

    /// Loop on each publisher matched with this subscription
    /// Extract all the already published messages
    /// Push them into the subscription buffer
    for (auto intra_process_publisher_id : publishers_set) {
      auto weak_publisher = impl_->get_publisher(intra_process_publisher_id);
      auto publisher = weak_publisher.lock();
      if (!publisher) {
        throw std::runtime_error("publisher has unexpectedly gone out of scope");
      }

      auto pub_buffer_base = publisher->get_intra_process_buffer();
      std::shared_ptr<PublisherBufferT> pub_buffer =
        std::static_pointer_cast<PublisherBufferT>(pub_buffer_base);

      auto stored_messages = pub_buffer->get_all();

      // TODO: maybe is better to get all the messages from all publishers before pushing
      for (auto message : stored_messages) {
        sub_buffer->add(message);
      }
    }

    // TODO: the condition variable should be triggered only if something has been really added
    subscription->trigger_guard_condition();
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
    using IntraProcessBufferT = typename intra_process_buffer::IntraProcessBuffer<MessageT>;

    for (auto id : subscription_ids) {
      auto weak_subscription = impl_->get_subscription(id);
      auto subscription = weak_subscription.lock();
      if (!subscription) {
        throw std::runtime_error("subscription has unexpectedly gone out of scope");
      }

      auto buffer_base = subscription->get_intra_process_buffer();
      std::shared_ptr<IntraProcessBufferT> buffer =
        std::static_pointer_cast<IntraProcessBufferT>(buffer_base);

      buffer->add(message);
      subscription->trigger_guard_condition();
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
    using IntraProcessBufferT = typename intra_process_buffer::IntraProcessBuffer<MessageT>;

    for (auto it = subscription_ids.begin(); it != subscription_ids.end(); it++) {
      auto weak_subscription = impl_->get_subscription(*it);
      auto subscription = weak_subscription.lock();
      if (!subscription) {
        throw std::runtime_error("subscription has unexpectedly gone out of scope");
      }

      auto buffer_base = subscription->get_intra_process_buffer();
      std::shared_ptr<IntraProcessBufferT> buffer = std::static_pointer_cast<IntraProcessBufferT>(
        buffer_base);

      if (std::next(it) == subscription_ids.end()) {
        // If this is the last subscription, give up ownership
        buffer->add(std::move(message));
      } else {
        // Copy the message since we have additional subscriptions to serve
        std::unique_ptr<MessageT, Deleter> copy_message = std::make_unique<MessageT>(*message);
        buffer->add(std::move(copy_message));
      }
      subscription->trigger_guard_condition();
    }
  }

  IntraProcessManagerImplBase::SharedPtr impl_;
};

}  // namespace intra_process_manager
}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_MANAGER_HPP_
