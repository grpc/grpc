//
//
// Copyright 2018 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"

#include "src/core/lib/gpr/useful.h"

// The subchannel key ID that is only used in test to make each key unique.
#define GRPC_ARG_SUBCHANNEL_KEY_TEST_ONLY_ID "grpc.subchannel_key_test_only_id"

namespace grpc_core {

TraceFlag grpc_subchannel_pool_trace(false, "subchannel_pool");

SubchannelKey::SubchannelKey(const grpc_channel_args* args) {
  // Remove the subchannel pool arg to break the circular reference between the
  // subchannel pool and the subchannel key, if any.
  args_ =
      grpc_channel_args_remove_and_normalize(args, GRPC_ARG_SUBCHANNEL_POOL);
}

SubchannelKey::~SubchannelKey() {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
}

SubchannelKey::SubchannelKey(const SubchannelKey& other) {
  args_ = grpc_channel_args_copy(other.args_);
}

SubchannelKey& SubchannelKey::operator=(const SubchannelKey& other) {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
  args_ = grpc_channel_args_copy(other.args_);
  return *this;
}

int SubchannelKey::Cmp(const SubchannelKey& other) const {
  return grpc_channel_args_compare(args_, other.args_);
}

namespace {

void* arg_copy(void* p) {
  auto* subchannel_pool = static_cast<SubchannelPoolInterface*>(p);
  subchannel_pool->Ref().release();
  return p;
}

void arg_destroy(void* p) {
  auto* subchannel_pool = static_cast<SubchannelPoolInterface*>(p);
  subchannel_pool->Unref();
}

int arg_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

const grpc_arg_pointer_vtable subchannel_pool_arg_vtable = {
    arg_copy, arg_destroy, arg_cmp};

}  // namespace

grpc_arg SubchannelPoolInterface::CreateChannelArg(
    SubchannelPoolInterface* subchannel_pool) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_SUBCHANNEL_POOL), subchannel_pool,
      &subchannel_pool_arg_vtable);
}

SubchannelPoolInterface*
SubchannelPoolInterface::GetSubchannelPoolFromChannelArgs(
    const grpc_channel_args* args) {
  const grpc_arg* arg = grpc_channel_args_find(args, GRPC_ARG_SUBCHANNEL_POOL);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) return nullptr;
  return static_cast<SubchannelPoolInterface*>(arg->value.pointer.p);
}

}  // namespace grpc_core
