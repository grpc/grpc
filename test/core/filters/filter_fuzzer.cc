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

#include <map>

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

struct Filter {
  absl::string_view name;
  absl::StatusOr<std::unique_ptr<ChannelFilter>> (*create)(
      ChannelArgs channel_args, ChannelFilter::Args filter_args);

  template <typename T>
  static Filter* Make(absl::string_view name) {
    return new Filter{
        name,
        [](ChannelArgs channel_args, ChannelFilter::Args filter_args)
            -> absl::StatusOr<std::unique_ptr<ChannelFilter>> {
          auto* args = channel_args.ToC();
          auto r = T::Create(args, filter_args);
          grpc_channel_args_destroy(args);
          if (!r.ok()) return r.status();
          return std::unique_ptr<ChannelFilter>(new T(std::move(*r)));
        }};
  }
};

#define MAKE_FILTER(name) Filter::Make<name>(#name)

const Filter* const kFilters[] = {
    MAKE_FILTER(ClientAuthorityFilter),
    MAKE_FILTER(HttpClientFilter),
    MAKE_FILTER(ClientAuthFilter),
    MAKE_FILTER(GrpcServerAuthzFilter),
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

absl::StatusOr<std::unique_ptr<ChannelFilter>> CreateFilter(
    absl::string_view name, ChannelArgs channel_args,
    ChannelFilter::Args filter_args) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(kFilters); ++i) {
    if (name == kFilters[i]->name)
      return kFilters[i]->create(std::move(channel_args), filter_args);
  }
  return absl::NotFoundError(absl::StrCat("Filter ", name, " not found"));
}

class FuzzCall {};

class MainLoop {
 public:
  MainLoop(std::unique_ptr<ChannelFilter> filter)
      : filter_(std::move(filter)) {}

  void Run(const filter_fuzzer::Action& action) {
    switch (action.type_case()) {
      case filter_fuzzer::Action::TYPE_NOT_SET:
        break;
      case filter_fuzzer::Action::kCancel:
        calls_.erase(action.call());
        break;
      case filter_fuzzer::Action::kCreateCall:
        calls_.emplace(action.call(), absl::make_unique<FuzzCall>());
        break;
    }
    /*
  auto activity = grpc_core::MakeActivity(
      [&filter]() {
        return (*filter)->MakeCallPromise(grpc_core::CallArgs{nullptr, nullptr},
                                          [](grpc_core::CallArgs) { abort(); })
      },
      ExecCtxWakeupScheduler(), [](grpc_core::ServerMetadataHandle) {});
*/
  }

 private:
  std::unique_ptr<ChannelFilter> filter_;
  std::map<uint32_t, std::unique_ptr<FuzzCall>> calls_;
};

}  // namespace
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const filter_fuzzer::Msg& msg) {
  auto filter = grpc_core::CreateFilter(
      msg.filter(), grpc_core::LoadChannelArgs(msg.channel_args()),
      grpc_core::ChannelFilter::Args());
  if (!filter.ok()) return;
  grpc_core::MainLoop main_loop(std::move(*filter));
  for (const auto& action : msg.actions()) {
    main_loop.Run(action);
  }
}
