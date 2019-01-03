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

#include "src/core/ext/filters/client_channel/subchannel_key.h"

namespace grpc_core {

SubchannelKey::SubchannelKey(const grpc_subchannel_args* args) {
  Init(args, grpc_channel_args_normalize);
}

SubchannelKey::~SubchannelKey() {
  gpr_free(reinterpret_cast<grpc_channel_args*>(args_.filters));
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_.args));
}

SubchannelKey::SubchannelKey(const SubchannelKey& other) {
  Init(&other.args_, grpc_channel_args_copy);
}

SubchannelKey& SubchannelKey::operator=(const SubchannelKey& other) {
  gpr_free(reinterpret_cast<grpc_channel_args*>(args_.filters));
  grpc_channel_args_destroy(const_cast<grpc_channel_args*>(args_.args));
  Init(&other.args_, grpc_channel_args_copy);
  return *this;
}

int SubchannelKey::Cmp(const SubchannelKey& other) const {
  // To pretend the keys are different, return a non-zero value.
  if (GPR_UNLIKELY(force_different_)) return 1;
  int c = GPR_ICMP(args_.filter_count, other.args_.filter_count);
  if (c != 0) return c;
  if (args_.filter_count > 0) {
    c = memcmp(args_.filters, other.args_.filters,
               args_.filter_count * sizeof(*args_.filters));
    if (c != 0) return c;
  }
  return grpc_channel_args_compare(args_.args, other.args_.args);
}

void SubchannelKey::TestOnlySetForceDifferent(bool force_creation) {
  force_different_ = force_creation;
}

void SubchannelKey::Init(
    const grpc_subchannel_args* args,
    grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args)) {
  args_.filter_count = args->filter_count;
  if (args_.filter_count > 0) {
    args_.filters = static_cast<const grpc_channel_filter**>(
        gpr_malloc(sizeof(*args_.filters) * args_.filter_count));
    memcpy(reinterpret_cast<grpc_channel_filter*>(args_.filters), args->filters,
           sizeof(*args_.filters) * args_.filter_count);
  } else {
    args_.filters = nullptr;
  }
  args_.args = copy_channel_args(args->args);
}

bool SubchannelKey::force_different_ = false;

}  // namespace grpc_core
