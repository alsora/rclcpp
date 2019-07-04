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
#include "rclcpp/subscription_intra_process.hpp"
#include "rclcpp/visibility_control.hpp"

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
    SubscriptionIntraProcessBase::SharedPtr subscription) = 0;

  virtual void
  remove_subscription(uint64_t intra_process_subscription_id) = 0;

  virtual void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher) = 0;

  virtual void
  remove_publisher(uint64_t intra_process_publisher_id) = 0;

  virtual bool
  matches_any_publishers(const rmw_gid_t * id) const = 0;

  virtual size_t
  get_subscription_count(uint64_t intra_process_publisher_id) const = 0;

  virtual std::set<uint64_t>
  get_all_matching_publishers(uint64_t intra_process_subscription_id) = 0;

  virtual void
  get_subscription_ids_for_pub(
    std::set<uint64_t> & take_shared_ids,
    std::set<uint64_t> & take_owned_ids,
    uint64_t intra_process_publisher_id) const = 0;

  virtual SubscriptionIntraProcessBase::SharedPtr
  get_subscription(uint64_t intra_process_subscription_id) = 0;

private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImplBase)
};

template<typename Allocator = std::allocator<void>>
class IntraProcessManagerImpl : public IntraProcessManagerImplBase
{
private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImpl)

  struct SubscriptionInfo
  {
    SubscriptionInfo() = default;

    SubscriptionIntraProcessBase::SharedPtr subscription;
    rmw_qos_profile_t options;
    const char * topic_name;
    bool use_take_shared_method;
  };

  struct PublisherInfo
  {
    PublisherInfo() = default;

    PublisherBase::WeakPtr publisher;
    rmw_qos_profile_t options;
    const char * topic_name;
  };

  struct SplittedSubscriptions
  {
    std::set<uint64_t> take_shared_subscriptions;
    std::set<uint64_t> take_ownership_subscriptions;
  };

  template<typename T>
  using RebindAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

  using SubscriptionMap = std::unordered_map<
    uint64_t, SubscriptionInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, SubscriptionInfo>>>;

  using PublisherMap = std::unordered_map<
    uint64_t, PublisherInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, PublisherInfo>>>;

  using PublisherToSubscriptionIdsMap = std::unordered_map<
    uint64_t, SplittedSubscriptions,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, SplittedSubscriptions>>>;

  PublisherToSubscriptionIdsMap pub_to_subs_;
  SubscriptionMap subscriptions_;
  PublisherMap publishers_;

  void insert_sub_id_for_pub(uint64_t sub_id, uint64_t pub_id, bool use_take_shared_method)
  {
    if (use_take_shared_method) {
      pub_to_subs_[pub_id].take_shared_subscriptions.insert(sub_id);
    } else {
      pub_to_subs_[pub_id].take_ownership_subscriptions.insert(sub_id);
    }
  }

  bool can_communicate(PublisherInfo pub_info, SubscriptionInfo sub_info)
  {
    // publisher and subscription must be on the same topic
    if (strcmp(pub_info.topic_name, sub_info.topic_name) != 0) {
      return false;
    }

    // a reliable subscription can't be connected with a best effort publisher
    if (sub_info.options.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE &&
      pub_info.options.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT)
    {
      return false;
    }

    // a publisher and a subscription with different durability can't communicate
    if (sub_info.options.durability != pub_info.options.durability) {
      return false;
    }

    return true;
  }

public:
  IntraProcessManagerImpl() = default;
  ~IntraProcessManagerImpl() = default;

  void
  add_subscription(
    uint64_t id,
    SubscriptionIntraProcessBase::SharedPtr subscription)
  {
    if (subscriptions_.find(id) != subscriptions_.end()) {
      return;
    }

    subscriptions_[id].subscription = subscription;
    subscriptions_[id].topic_name = subscription->get_topic_name();
    subscriptions_[id].options = subscription->get_actual_qos();
    subscriptions_[id].use_take_shared_method =
      subscription->use_take_shared_method();

    // adds the subscription id to all the matchable publishers
    for (auto pair : publishers_) {
      if (can_communicate(pair.second, subscriptions_[id])) {
        insert_sub_id_for_pub(id, pair.first, subscriptions_[id].use_take_shared_method);
      }
    }
  }

  void
  remove_subscription(uint64_t intra_process_subscription_id)
  {
    subscriptions_.erase(intra_process_subscription_id);

    for (auto & pair : pub_to_subs_) {
      pair.second.take_shared_subscriptions.erase(intra_process_subscription_id);
      pair.second.take_ownership_subscriptions.erase(intra_process_subscription_id);
    }
  }

  void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher)
  {
    if (publishers_.find(id) != publishers_.end()) {
      return;
    }

    publishers_[id].publisher = publisher;
    publishers_[id].topic_name = publisher->get_topic_name();
    publishers_[id].options = publisher->get_actual_qos();

    // create an entry for the publisher id and populate with already existing subscriptions
    for (auto pair : subscriptions_) {
      if (can_communicate(publishers_[id], pair.second)) {
        insert_sub_id_for_pub(pair.first, id, pair.second.use_take_shared_method);
      }
    }
  }

  void
  remove_publisher(uint64_t intra_process_publisher_id)
  {
    publishers_.erase(intra_process_publisher_id);
    pub_to_subs_.erase(intra_process_publisher_id);
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
    auto publisher_it = pub_to_subs_.find(intra_process_publisher_id);
    if (publisher_it == pub_to_subs_.end()) {
      // Publisher is either invalid or no longer exists.
      return 0;
    }

    auto count =
      publisher_it->second.take_shared_subscriptions.size() +
      publisher_it->second.take_ownership_subscriptions.size();

    return count;
  }

  void
  get_subscription_ids_for_pub(
    std::set<uint64_t> & take_shared_ids,
    std::set<uint64_t> & take_owned_ids,
    uint64_t intra_process_publisher_id) const
  {
    auto publisher_it = pub_to_subs_.find(intra_process_publisher_id);
    if (publisher_it == pub_to_subs_.end()) {
      // Publisher is either invalid or no longer exists.
      return;
    }

    take_shared_ids = publisher_it->second.take_shared_subscriptions;
    take_owned_ids = publisher_it->second.take_ownership_subscriptions;
  }

  std::set<uint64_t>
  get_all_matching_publishers(uint64_t intra_process_subscription_id)
  {
    std::set<uint64_t> res;

    for (auto pair : pub_to_subs_) {
      auto publisher_id = pair.first;
      auto ownership_subs = pair.second.take_ownership_subscriptions;
      auto shared_subs = pair.second.take_shared_subscriptions;
      if (ownership_subs.find(intra_process_subscription_id) != ownership_subs.end() ||
          shared_subs.find(intra_process_subscription_id) != shared_subs.end())
      {
        res.insert(publisher_id);
      }
    }

    return res;
  }

  SubscriptionIntraProcessBase::SharedPtr
  get_subscription(uint64_t intra_process_subscription_id)
  {
    auto subscription_it = subscriptions_.find(intra_process_subscription_id);
    if (subscription_it == subscriptions_.end()) {
      std::cout<<"Looking for wrong sub id "<< intra_process_subscription_id<<std::endl;
      return std::shared_ptr<SubscriptionIntraProcessBase>(nullptr);
    } else {
      return subscription_it->second.subscription;
    }
  }
};

RCLCPP_PUBLIC
IntraProcessManagerImplBase::SharedPtr
create_default_impl();

}  // namespace intra_process_manager
}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_MANAGER_IMPL_HPP_
