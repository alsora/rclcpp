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

#include <memory>
#include <utility>

#include "gtest/gtest.h"

#include "rclcpp/intra_process_buffer.hpp"
#include "rclcpp/buffers/simple_queue_implementation.hpp"

#include <rcl_interfaces/msg/log.hpp>
#include <rcl_interfaces/msg/parameter.hpp>

/*
   Tests buffer creation and properly set use_take_shared_method
 */
TEST(TestMappedRingBuffer, creation) {
  using MessageT1 = rcl_interfaces::msg::Log;
  using SharedMsgT1 = std::shared_ptr<const MessageT1>;
  using UniqueMsgT1 = std::unique_ptr<MessageT1>;

  auto buffer_implementation =
    std::make_shared<rclcpp::intra_process_buffer::SimpleQueueImplementation<MessageT1>>(1);

  auto buffer_implementation_shared =
    std::make_shared<rclcpp::intra_process_buffer::SimpleQueueImplementation<SharedMsgT1>>(1);

  auto buffer_implementation_unique =
    std::make_shared<rclcpp::intra_process_buffer::SimpleQueueImplementation<UniqueMsgT1>>(1);

  auto typed_buffer_message =
    rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT1,
      MessageT1>(buffer_implementation);

  EXPECT_EQ(false, typed_buffer_message.use_take_shared_method());

  auto typed_buffer_shared =
    rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT1,
      SharedMsgT1>(buffer_implementation_shared);

  EXPECT_EQ(true, typed_buffer_shared.use_take_shared_method());

  auto typed_buffer_unique =
    rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT1,
      UniqueMsgT1>(buffer_implementation_unique);

  EXPECT_EQ(false, typed_buffer_unique.use_take_shared_method());
}

/**
 * Tests basic operations with an intra-process buffer storing shared_ptr<MessageT>
 * - Push a unique_ptr, extract requesting a shared_ptr
 * - Push a shared_ptr, extract requesting a shared_ptr
 */
TEST(TestMappedRingBuffer, shared_buffer_operation) {
  using MessageT = rcl_interfaces::msg::Log;
  using SharedMsgT = std::shared_ptr<const MessageT>;
  using UniqueMsgT = std::unique_ptr<MessageT>;

  auto buffer_implementation_shared =
    std::make_shared<rclcpp::intra_process_buffer::SimpleQueueImplementation<SharedMsgT>>(1);

  auto typed_buffer_shared =
    rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT,
      SharedMsgT>(buffer_implementation_shared);

  SharedMsgT extracted_msg;

  UniqueMsgT unique_msg;
  auto original_message_pointer = reinterpret_cast<std::uintptr_t>(unique_msg.get());
  typed_buffer_shared.add(std::move(unique_msg));
  typed_buffer_shared.consume(extracted_msg);
  auto extracted_message_pointer = reinterpret_cast<std::uintptr_t>(extracted_msg.get());
  EXPECT_EQ(original_message_pointer, extracted_message_pointer);

  SharedMsgT shared_msg;
  original_message_pointer = reinterpret_cast<std::uintptr_t>(shared_msg.get());
  typed_buffer_shared.add(shared_msg);
  typed_buffer_shared.consume(extracted_msg);
  extracted_message_pointer = reinterpret_cast<std::uintptr_t>(extracted_msg.get());
  EXPECT_EQ(original_message_pointer, extracted_message_pointer);
}

/**
 * Tests basic operations with an intra-process buffer storing unique_ptr<MessageT>
 */
TEST(TestMappedRingBuffer, unique_buffer_operation) {
  using MessageT = rcl_interfaces::msg::Log;
  using SharedMsgT = std::shared_ptr<const MessageT>;
  using UniqueMsgT = std::unique_ptr<MessageT>;

  auto buffer_implementation_shared =
    std::make_shared<rclcpp::intra_process_buffer::SimpleQueueImplementation<UniqueMsgT>>(1);

  auto typed_buffer_shared =
    rclcpp::intra_process_buffer::TypedIntraProcessBuffer<MessageT,
      UniqueMsgT>(buffer_implementation_shared);

  UniqueMsgT extracted_msg;

  UniqueMsgT unique_msg;
  auto original_message_pointer = reinterpret_cast<std::uintptr_t>(unique_msg.get());
  typed_buffer_shared.add(std::move(unique_msg));
  typed_buffer_shared.consume(extracted_msg);
  auto extracted_message_pointer = reinterpret_cast<std::uintptr_t>(extracted_msg.get());
  EXPECT_EQ(original_message_pointer, extracted_message_pointer);

  SharedMsgT shared_msg;
  original_message_pointer = reinterpret_cast<std::uintptr_t>(shared_msg.get());
  EXPECT_THROW(typed_buffer_shared.add(shared_msg), std::runtime_error);
}
