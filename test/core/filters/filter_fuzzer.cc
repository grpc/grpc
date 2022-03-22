// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/authorization/grpc_server_authz_filter.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/filters/filter_fuzzer.pb.h"

bool squelch = true;

namespace grpc_core {
namespace {

const grpc_channel_filter* const kFilters[] = {
    &grpc_core::ClientAuthorityFilter::kFilter,
    &grpc_core::ClientAuthFilter::kFilter,
    &grpc_core::HttpClientFilter::kFilter,
    &grpc_core::GrpcServerAuthzFilter::kFilterVtable,
};

template <typename FuzzerChannelArgs>
ChannelArgs LoadChannelArgs(const FuzzerChannelArgs& fuzz_args) {
  ChannelArgs args;
  for (const auto& arg : fuzz_args) {
    switch (arg.value_case()) {
      case filter_fuzzer::ChannelArg::VALUE_NOT_SET:
        break;
      case filter_fuzzer::ChannelArg::kStr:
        args = args.Set(arg.key(), arg.str());
        break;
      case filter_fuzzer::ChannelArg::kI:
        args = args.Set(arg.key(), arg.i());
        break;
      case filter_fuzzer::ChannelArg::kResourceQuota: {
        auto rq = grpc_core::MakeResourceQuota("test");
        rq->memory_quota()->SetSize(arg.resource_quota());
        args = args.SetObject(std::move(rq));
        break;
      }
    }
  }
  return args;
}

const grpc_channel_filter* FindFilter(absl::string_view name) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(kFilters); ++i) {
    if (name == kFilters[i]->name) return kFilters[i];
  }
  return nullptr;
}

}  // namespace
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const filter_fuzzer::Msg& msg) {
  auto* filter = grpc_core::FindFilter(msg.filter());
  if (filter == nullptr) return;
  grpc_channel_element elem = {
      filter, gpr_malloc_aligned(filter->sizeof_channel_data, 16)};
  auto* channel_args = grpc_core::LoadChannelArgs(msg.channel_args()).ToC();

  grpc_channel_args_destroy(channel_args);
}
