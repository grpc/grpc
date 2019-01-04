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

#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

TraceFlag grpc_subchannel_pool_trace(false, "subchannel_pool");

SubchannelKey::SubchannelKey(const grpc_channel_args* args) {
  Init(args, grpc_channel_args_normalize);
}

SubchannelKey::~SubchannelKey() {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
}

SubchannelKey::SubchannelKey(const SubchannelKey& other) {
  Init(other.args_, grpc_channel_args_copy);
}

SubchannelKey& SubchannelKey::operator=(const SubchannelKey& other) {
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_));
  Init(other.args_, grpc_channel_args_copy);
  return *this;
}

int SubchannelKey::Cmp(const SubchannelKey& other) const {
  // To pretend the keys are different, return a non-zero value.
  if (GPR_UNLIKELY(force_different_)) return 1;
  return grpc_channel_args_compare(args_, other.args_);
}

void SubchannelKey::TestOnlySetForceDifferent(bool force_creation) {
  force_different_ = force_creation;
}

void SubchannelKey::Init(
    const grpc_channel_args* args,
    grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args)) {
  args_ = copy_channel_args(args);
}

bool SubchannelKey::force_different_ = false;

}  // namespace grpc_core
