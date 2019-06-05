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
  add_subscription(uint64_t id, SubscriptionBase::SharedPtr subscription) = 0;

  virtual void
  remove_subscription(uint64_t intra_process_subscription_id) = 0;

  virtual void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher) = 0;

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

private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImplBase)
};

template<typename Allocator = std::allocator<void>>
class IntraProcessManagerImpl : public IntraProcessManagerImplBase
{
public:
  IntraProcessManagerImpl() = default;
  ~IntraProcessManagerImpl() = default;

  void
  add_subscription(uint64_t id, SubscriptionBase::SharedPtr subscription)
  {
    subscriptions_[id].subscription = subscription;
    subscriptions_[id].topic_name = subscription->get_topic_name();

    subscription_ids_by_topic_[fixed_size_string(subscription->get_topic_name())].insert(id);
  }

  void
  remove_subscription(uint64_t intra_process_subscription_id)
  {
    subscriptions_.erase(intra_process_subscription_id);
    for (auto & pair : subscription_ids_by_topic_) {
      pair.second.erase(intra_process_subscription_id);
    }
  }

  void add_publisher(
    uint64_t id,
    PublisherBase::SharedPtr publisher)
  {
    PublisherInfo pub_info;
    pub_info.publisher = publisher;
    pub_info.topic_name = publisher->get_topic_name();

    publishers_[id].publisher = publisher;
    publishers_[id].topic_name = publisher->get_topic_name();
  }

  void
  remove_publisher(uint64_t intra_process_publisher_id)
  {
    publishers_.erase(intra_process_publisher_id);
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
    PublisherInfo & info = it->second;
    /*
    auto publisher = info.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    */

    // Figure out what subscriptions should receive the message.
    auto & destined_subscriptions =
      subscription_ids_by_topic_[fixed_size_string(info.topic_name)];

    for (auto id : destined_subscriptions){
      SubscriptionInfo & info = subscriptions_[id];
      auto subscriber = info.subscription.lock();
      if (!subscriber) {
        throw std::runtime_error("subscriber has unexpectedly gone out of scope");
      }

      subscriber->add_message_to_queue(msg);
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
    PublisherInfo & info = it->second;
    /*
    auto publisher = info.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    */

    // Figure out what subscriptions should receive the message.
    auto & destined_subscriptions =
      subscription_ids_by_topic_[fixed_size_string(info.topic_name)];

    for (auto iter = destined_subscriptions.begin(); iter != destined_subscriptions.end(); ++iter){
      auto id = *iter;
      SubscriptionInfo & info = subscriptions_[id];
      auto subscriber = info.subscription.lock();
      if (!subscriber) {
        throw std::runtime_error("subscriber has unexpectedly gone out of scope");
      }
      // if this is the last iteration, we give up ownership
      if (std::next(iter) == destined_subscriptions.end()) {
        subscriber->add_message_to_queue(msg, false);
      }
      // otherwise we copy the message
      else{
        subscriber->add_message_to_queue(msg, true);
      }
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
    auto publisher = publisher_it->second.publisher.lock();
    if (!publisher) {
      throw std::runtime_error("publisher has unexpectedly gone out of scope");
    }
    auto sub_map_it =
      subscription_ids_by_topic_.find(fixed_size_string(publisher->get_topic_name()));
    if (sub_map_it == subscription_ids_by_topic_.end()) {
      // No intraprocess subscribers
      return 0;
    }
    return sub_map_it->second.size();
  }

private:
  RCLCPP_DISABLE_COPY(IntraProcessManagerImpl)

  using FixedSizeString = std::array<char, RMW_TOPIC_MAX_NAME_LENGTH + 1>;

  FixedSizeString
  fixed_size_string(const char * str) const
  {
    FixedSizeString ret;
    size_t size = std::strlen(str) + 1;
    if (size > ret.size()) {
      throw std::runtime_error("failed to copy topic name");
    }
    std::memcpy(ret.data(), str, size);
    return ret;
  }
  struct strcmp_wrapper
  {
    bool
    operator()(const FixedSizeString lhs, const FixedSizeString rhs) const
    {
      return std::strcmp(lhs.data(), rhs.data()) < 0;
    }
  };

  struct SubscriptionInfo
  {
    RCLCPP_DISABLE_COPY(SubscriptionInfo)

    SubscriptionInfo() = default;

    SubscriptionBase::WeakPtr subscription;
    const char* topic_name;
  };

  struct PublisherInfo
  {
    RCLCPP_DISABLE_COPY(PublisherInfo)

    PublisherInfo() = default;

    PublisherBase::WeakPtr publisher;
    const char* topic_name;
  };

  template<typename T>
  using RebindAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

  using AllocSet = std::set<uint64_t, std::less<uint64_t>, RebindAlloc<uint64_t>>;

  using SubscriptionMap = std::unordered_map<
    uint64_t, SubscriptionInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, SubscriptionInfo>>>;

  using IDTopicMap = std::map<
    FixedSizeString,
    AllocSet,
    strcmp_wrapper,
    RebindAlloc<std::pair<const FixedSizeString, AllocSet>>>;

  using PublisherMap = std::unordered_map<
    uint64_t, PublisherInfo,
    std::hash<uint64_t>, std::equal_to<uint64_t>,
    RebindAlloc<std::pair<const uint64_t, PublisherInfo>>>;

  /*
  using TopicSubscriptionMap = std::unordered_map<
    std::string,
    SubscriptionMap,
    std::hash<std::string>, std::equal_to<std::string>,
    RebindAlloc<std::pair<const std::string, SubscriptionMap>>>;

  using TopicPublisherMap = std::unordered_map<
    std::string,
    PublisherMap,
    std::hash<std::string>, std::equal_to<std::string>,
    RebindAlloc<std::pair<const std::string, PublisherMap>>>;
  */

  IDTopicMap subscription_ids_by_topic_;
  SubscriptionMap subscriptions_;
  PublisherMap publishers_;

  std::mutex runtime_mutex_;
};

RCLCPP_PUBLIC
IntraProcessManagerImplBase::SharedPtr
create_default_impl();

}  // namespace intra_process_manager
}  // namespace rclcpp

#endif  // RCLCPP__INTRA_PROCESS_MANAGER_IMPL_HPP_
