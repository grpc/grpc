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

namespace grpc_core {

TraceFlag grpc_subchannel_pool_trace(false, "subchannel_pool");

SubchannelKey::SubchannelKey(const grpc_channel_args* args) {
  Init(args, grpc_channel_args_normalize);
  static size_t next_id = 0;
  if (GPR_UNLIKELY(gpr_atm_no_barrier_load(&force_different_))) {
    test_only_id_ = next_id++;
  }
}

SubchannelKey::~SubchannelKey() {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
}

SubchannelKey::SubchannelKey(const SubchannelKey& other) {
  Init(other.args_, grpc_channel_args_copy);
  if (GPR_UNLIKELY(gpr_atm_no_barrier_load(&force_different_))) {
    test_only_id_ = other.test_only_id_;
  }
}

SubchannelKey& SubchannelKey::operator=(const SubchannelKey& other) {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
  Init(other.args_, grpc_channel_args_copy);
  if (GPR_UNLIKELY(gpr_atm_no_barrier_load(&force_different_))) {
    test_only_id_ = other.test_only_id_;
  }
  return *this;
}

int SubchannelKey::Cmp(const SubchannelKey& other) const {
  // Return 0 if the ID's are the same.
  if (GPR_UNLIKELY(gpr_atm_no_barrier_load(&force_different_))) {
    return test_only_id_ != other.test_only_id_;
  }
  return grpc_channel_args_compare(args_, other.args_);
}

void SubchannelKey::TestOnlySetForceDifferent(bool force_creation) {
  gpr_atm_no_barrier_store(&force_different_, force_creation);
}

void SubchannelKey::Init(
    const grpc_channel_args* args,
    grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args)) {
  args_ = copy_channel_args(args);
}

gpr_atm SubchannelKey::force_different_ = 0;

namespace {

void* arg_copy(void* p) { return p; }

void arg_destroy(void* p) {}

int arg_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

const grpc_arg_pointer_vtable subchannel_pool_arg_vtable = {
    arg_copy, arg_destroy, arg_cmp};

}  // namespace

SubchannelPoolInterface*
SubchannelPoolInterface::GetSubchannelPoolFromChannelArgs(
    const grpc_channel_args* args) {
  const grpc_arg* arg = grpc_channel_args_find(args, GRPC_ARG_SUBCHANNEL_POOL);
  if (arg) {
    GPR_ASSERT(arg->type == GRPC_ARG_POINTER);
    return static_cast<SubchannelPoolInterface*>(arg->value.pointer.p);
  }
  return nullptr;
}

grpc_arg SubchannelPoolInterface::CreateChannelArg(
    grpc_core::SubchannelPoolInterface* subchannel_pool) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_SUBCHANNEL_POOL), subchannel_pool,
      &subchannel_pool_arg_vtable);
}

}  // namespace grpc_core
