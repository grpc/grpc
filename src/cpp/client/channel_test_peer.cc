/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/test/channel_test_peer.h>

#include <grpc/support/sync.h>
#include <grpcpp/channel.h>

#include "src/core/lib/surface/channel.h"

namespace grpc {
namespace testing {

int ChannelTestPeer::registered_calls() const {
  grpc_core::MutexLock lock(&channel_->c_channel_->registration_table->mu);
  return static_cast<int>(channel_->c_channel_->registration_table->map.size());
}

int ChannelTestPeer::registration_attempts() const {
  grpc_core::MutexLock lock(&channel_->c_channel_->registration_table->mu);
  return channel_->c_channel_->registration_table->method_registration_attempts;
}

}  // namespace testing
}  // namespace grpc
