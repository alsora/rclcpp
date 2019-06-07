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

#ifndef RCLCPP__INTRA_PROCESS_MANAGER_IMPL_HPP_
#define RCLCPP__INTRA_PROCESS_MANAGER_IMPL_HPP_

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "rmw/validate_full_topic_name.h"

#include "rclcpp/macros.hpp"
#include "rclcpp/publisher_base.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/visibility_control.hpp"

#include "rclcpp/intra_process_setting.hpp"

namespace rclcpp
{
namespace intra_process_manager
{

class IntraProcessManagerImplBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(IntraProcessManagerImplBase)

  IntraProcessManagerImplBase() = default;
  virtual ~IntraProcessManagerImplBase() = default;

  virtual void
  add_subscription(
    uint64_t id,
    SubscriptionBase::SharedPtr subscription,
    rcl_subscription_options_t options) = 0;

  virtual void
  remove_subscription(uint64_t intra_process_subscription_id) = 0;

  virtual void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher,
    rcl_publisher_options_t options) = 0;

  virtual void
  remove_publisher(uint64_t intra_process_publisher_id) = 0;

  virtual void
  optimized_ipc_publish_shared(
    uint64_t intra_process_publisher_id,
    std::shared_ptr<const void> msg) = 0;

  virtual void
  optimized_ipc_publish_unique(
    uint64_t intra_process_publisher_id,
    void* msg) = 0;

  virtual bool
  matches_any_publishers(const rmw_gid_t * id) const = 0;

  virtual size_t
  get_subscription_count(uint64_t intra_process_publisher_id) const = 0;

  virtual const std::vector<uint64_t>
  get_subscription_ids_for_pub(uint64_t intra_process_publisher_id) const = 0;

private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImplBase)
};

template<typename Allocator = std::allocator<void>>
class IntraProcessManagerImpl : public IntraProcessManagerImplBase
{

private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImpl)

  template<typename T>
  using RebindAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

  struct SubscriptionInfo
  {
    SubscriptionInfo() = default;

    SubscriptionBase::WeakPtr subscription;
    rcl_subscription_options_t options;
    const char* topic_name;
    bool use_take_shared_method;
  };

  struct PublisherInfo
  {
    PublisherInfo() = default;

    PublisherBase::WeakPtr publisher;
    rcl_publisher_options_t options;
    const char* topic_name;
  };

  using SubscriptionMap = std::unordered_map<
    uint64_t, SubscriptionInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, SubscriptionInfo>>>;

  using PublisherMap = std::unordered_map<
    uint64_t, PublisherInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, PublisherInfo>>>;

  struct PublisherToSubscriptionsMap
  {
    void insert_sub_id_for_pub(uint64_t sub_id, uint64_t pub_id, bool priority)
    {
      //first check if the element is not already there
      if (std::find(map_[pub_id].begin(), map_[pub_id].end(), sub_id) != map_[pub_id].end()){
        return;
      }
      auto it = map_[pub_id].end();
      if (priority){
        it = map_[pub_id].begin();
      }
      map_[pub_id].insert(it, sub_id);
    }

    void remove_sub(uint64_t sub_id)
    {
      for (auto & pair : map_) {
        auto it = std::find(pair.second.begin(), pair.second.end(), sub_id);
        if (it != pair.second.end()){
          pair.second.erase(it);
        }
      }
    }

    void remove_pub(uint64_t pub_id)
    {
      map_.erase(pub_id);
    }

    const std::vector<uint64_t> get_sub_ids_for_pub(uint64_t pub_id) const
    {
      auto subs_it = map_.find(pub_id);
      if (subs_it != map_.end()){
        return subs_it->second;
      }
      else{
        return std::vector<uint64_t>();
      }
    }

    size_t count_subs_for_pub(uint64_t pub_id) const
    {
      auto subs_it = map_.find(pub_id);
      if (subs_it != map_.end()){
        return subs_it->second.size();
      }
      else{
        return 0;
      }
    }

  private:
    std::unordered_map<
      uint64_t, std::vector<uint64_t>,
      std::hash<uint64_t>, std::equal_to<uint64_t>,
      RebindAlloc<std::pair<const uint64_t, std::vector<uint64_t>>>
      > map_;
  };


  PublisherToSubscriptionsMap pub_to_subs_;
  SubscriptionMap subscriptions_;
  PublisherMap publishers_;

public:
  IntraProcessManagerImpl() = default;
  ~IntraProcessManagerImpl() = default;

  bool can_communicate(PublisherInfo pub_info, SubscriptionInfo sub_info)
  {
    // publisher and subscription must be on the same topic
    if (strcmp(pub_info.topic_name, sub_info.topic_name) != 0){
      return false;
    }

    // a reliable subscription can't be connected with a best effort publisher
    if (sub_info.options.qos.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE &&
      pub_info.options.qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT){
      return false;
    }

    // a transient local subscription can't be connected with a volatile publisher
    if (sub_info.options.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL &&
      pub_info.options.qos.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE){
      return false;
    }

    return true;
  }

  void get_subscription(uint64_t id)
  {
    return subscriptions_[id];
  }



  void
  add_subscription(
    uint64_t id,
    SubscriptionBase::SharedPtr subscription,
    rcl_subscription_options_t options)
  {
    subscriptions_[id].subscription = subscription;
    subscriptions_[id].topic_name = subscription->get_topic_name();
    subscriptions_[id].use_take_shared_method = subscription->use_take_shared_method();
    subscriptions_[id].options = options;

    // adds the subscription id to all the matchable publishers
    for (auto pair : publishers_){
      if (can_communicate(pair.second, subscriptions_[id])){
        pub_to_subs_.insert_sub_id_for_pub(id, pair.first, subscriptions_[id].use_take_shared_method);
      }
    }
  }

  void
  remove_subscription(uint64_t intra_process_subscription_id)
  {
    subscriptions_.erase(intra_process_subscription_id);
    pub_to_subs_.remove_sub(intra_process_subscription_id);
  }

  void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher,
    rcl_publisher_options_t options)
  {
    publishers_[id].publisher = publisher;
    publishers_[id].topic_name = publisher->get_topic_name();
    publishers_[id].options = options;

    // create an entry for the publisher id and populate with already existing subscriptions
    for (auto pair : subscriptions_){
      if (can_communicate(publishers_[id], pair.second)){
        pub_to_subs_.insert_sub_id_for_pub(pair.first, id, pair.second.use_take_shared_method);
      }
    }
  }

  void
  remove_publisher(uint64_t intra_process_publisher_id)
  {
    publishers_.erase(intra_process_publisher_id);
    pub_to_subs_.remove_pub(intra_process_publisher_id);
  }

  void
  optimized_ipc_publish_shared(
    uint64_t intra_process_publisher_id,
    std::shared_ptr<const void> msg)
  {
    auto it = publishers_.find(intra_process_publisher_id);
    if (it == publishers_.end()) {
      throw std::runtime_error("optimized_ipc_publish_shared called with invalid publisher id");
    }

    /*
    PublisherInfo & info = it->second;
    auto publisher = info.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    */

    // Figure out what subscriptions should receive the message.
    // They are already ordered: first the ones requiring ownership then the others.
    auto & destined_subscriptions =
      pub_to_subs_.get_sub_ids_for_pub(intra_process_publisher_id);

    for (uint64_t id : destined_subscriptions){
      SubscriptionInfo & info = subscriptions_[id];
      auto subscriber = info.subscription.lock();
      if (!subscriber) {
        throw std::runtime_error("subscriber has unexpectedly gone out of scope");
      }

      subscriber->add_shared_message_to_queue(msg);
    }
  }

  void
  optimized_ipc_publish_unique(
    uint64_t intra_process_publisher_id,
    void* msg)
  {
    auto it = publishers_.find(intra_process_publisher_id);
    if (it == publishers_.end()) {
      throw std::runtime_error("pass_message_to_buffers called with invalid publisher id");
    }

    /*
    PublisherInfo & info = it->second;
    auto publisher = info.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    */

    // Figure out what subscriptions should receive the message.
    // They are already ordered: first the ones requiring ownership then the others.
    auto & destined_subscriptions =
      pub_to_subs_.get_sub_ids_for_pub(intra_process_publisher_id);

    for (auto iter = destined_subscriptions.begin(); iter != destined_subscriptions.end(); ++iter){
      auto id = *iter;
      SubscriptionInfo & info = subscriptions_[id];
      auto subscriber = info.subscription.lock();
      if (!subscriber) {
        throw std::runtime_error("subscriber has unexpectedly gone out of scope");
      }

      bool can_be_taken = false;
      // if this is the last iteration, we give up ownership
      if (std::next(iter) == destined_subscriptions.end()) {
        can_be_taken = true;
      }

      subscriber->add_owned_message_to_queue(msg, can_be_taken);
    }
  }

  bool
  matches_any_publishers(const rmw_gid_t * id) const
  {
    for (auto & publisher_pair : publishers_) {
      auto publisher = publisher_pair.second.publisher.lock();
      if (!publisher) {
        continue;
      }
      if (*publisher.get() == id) {
        return true;
      }
    }
    return false;
  }

  size_t
  get_subscription_count(uint64_t intra_process_publisher_id) const
  {
    auto publisher_it = publishers_.find(intra_process_publisher_id);
    if (publisher_it == publishers_.end()) {
      // Publisher is either invalid or no longer exists.
      return 0;
    }
    /*
    auto publisher = publisher_it->second.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    */
    auto sub_count = pub_to_subs_.count_subs_for_pub(intra_process_publisher_id);

    return sub_count;
  }

  const std::vector<uint64_t>
  get_subscription_ids_for_pub(uint64_t intra_process_publisher_id) const
  {
    auto publisher_it = publishers_.find(intra_process_publisher_id);
    if (publisher_it == publishers_.end()) {
      // Publisher is either invalid or no longer exists.
      return std::vector<uint64_t>();
    }

    return pub_to_subs_.get_sub_ids_for_pub(intra_process_publisher_id);
  }
};

RCLCPP_PUBLIC
IntraProcessManagerImplBase::SharedPtr
create_default_impl();

}  // namespace intra_process_manager
}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_MANAGER_IMPL_HPP_
