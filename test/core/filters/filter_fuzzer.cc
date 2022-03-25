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
#include "src/core/lib/transport/transport_impl.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/filters/filter_fuzzer.pb.h"

bool squelch = true;

namespace grpc_core {
namespace {

static const grpc_transport_vtable kFakeTransportVTable = {
    // sizeof_stream
    0,
    // name
    "fake_transport",
    // init_stream
    [](grpc_transport* self, grpc_stream* stream,
       grpc_stream_refcount* refcount, const void* server_data,
       grpc_core::Arena* arena) -> int { abort(); },
    // make_call_promise
    [](grpc_transport* self, grpc_core::ClientMetadataHandle initial_metadata)
        -> grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> {
      abort();
    },
    // set_pollset
    [](grpc_transport* self, grpc_stream* stream, grpc_pollset* pollset) {
      abort();
    },
    // set_pollset_set
    [](grpc_transport* self, grpc_stream* stream,
       grpc_pollset_set* pollset_set) { abort(); },
    // perform_stream_op
    [](grpc_transport* self, grpc_stream* stream,
       grpc_transport_stream_op_batch* op) { abort(); },
    // perform_op
    [](grpc_transport* self, grpc_transport_op* op) { abort(); },
    // destroy_stream
    [](grpc_transport* self, grpc_stream* stream,
       grpc_closure* then_schedule_closure) { abort(); },
    // destroy
    [](grpc_transport* self) { abort(); },
    // get_endpoint
    [](grpc_transport* self) -> grpc_endpoint* { abort(); },
};

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

struct GlobalObjects {
  ResourceQuotaRefPtr resource_quota = MakeResourceQuota("test");
  grpc_transport transport{&kFakeTransportVTable};
};

template <typename FuzzerChannelArgs>
ChannelArgs LoadChannelArgs(const FuzzerChannelArgs& fuzz_args,
                            GlobalObjects* globals) {
  ChannelArgs args = ChannelArgs().SetObject(ResourceQuota::Default());
  for (const auto& arg : fuzz_args) {
    if (arg.key() == ResourceQuota::ChannelArgName()) {
      if (arg.value_case() == filter_fuzzer::ChannelArg::kResourceQuota) {
        args = args.SetObject(globals->resource_quota);
      }
    } else if (arg.key() == GRPC_ARG_TRANSPORT) {
      if (arg.value_case() == filter_fuzzer::ChannelArg::kTransport) {
        static const grpc_arg_pointer_vtable vtable = {
            // copy
            [](void* p) { return p; },
            // destroy
            [](void*) {},
            // cmp
            [](void* a, void* b) { return QsortCompare(a, b); },
        };
        args = args.Set(GRPC_ARG_TRANSPORT,
                        ChannelArgs::Pointer(&globals->transport, &vtable));
      }
    } else {
      switch (arg.value_case()) {
        case filter_fuzzer::ChannelArg::VALUE_NOT_SET:
          break;
        case filter_fuzzer::ChannelArg::kStr:
          args = args.Set(arg.key(), arg.str());
          break;
        case filter_fuzzer::ChannelArg::kI:
          args = args.Set(arg.key(), arg.i());
          break;
        case filter_fuzzer::ChannelArg::kResourceQuota:
        case filter_fuzzer::ChannelArg::kTransport:
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
    if (name == kFilters[i]->name) {
      return kFilters[i]->create(std::move(channel_args), filter_args);
    }
  }
  return absl::NotFoundError(absl::StrCat("Filter ", name, " not found"));
}

class MainLoop {
 public:
  MainLoop(std::unique_ptr<ChannelFilter> filter, ChannelArgs channel_args)
      : memory_allocator_(channel_args.GetObject<ResourceQuota>()
                              ->memory_quota()
                              ->CreateMemoryAllocator("test")),
        filter_(std::move(filter)) {}

  void Run(const filter_fuzzer::Action& action, GlobalObjects* globals) {
    for (auto id: absl::exchange(wakeups_, {})) {
      if (auto* call = GetCall(id)) call->Wakeup();
    }
    switch (action.type_case()) {
      case filter_fuzzer::Action::TYPE_NOT_SET:
        break;
      case filter_fuzzer::Action::kCancel:
        calls_.erase(action.call());
        break;
      case filter_fuzzer::Action::kCreateCall:
        calls_.emplace(
            action.call(),
            absl::make_unique<Call>(this, action.call(), action.create_call()));
        break;
      case filter_fuzzer::Action::kReceiveTrailingMetadata:
        if (auto* call = GetCall(action.call())) {
          call->RecvTrailingMetadata(action.receive_trailing_metadata());
        } break;
    }
  }

 private:
  class WakeCall final : public Wakeable {
   public:
    WakeCall(MainLoop* main_loop, uint32_t id)
        : main_loop_(main_loop), id_(id) {}
    void Wakeup() override {
      for (const uint32_t already : main_loop_->wakeups_) {
        if (already == id_) return;
      }
      main_loop_->wakeups_.push_back(id_);
      delete this;
    }
    void Drop() override { delete this; }

   private:
    MainLoop* const main_loop_;
    uint32_t id_;
  };

  class Call final : public Activity {
   public:
    Call(MainLoop* main_loop, uint32_t id,
         const filter_fuzzer::Metadata& client_initial_metadata)
        : main_loop_(main_loop), id_(id) {
      ScopedContext context(this);
      promise_ = main_loop_->filter_->MakeCallPromise(
          CallArgs{std::move(*LoadMetadata(client_initial_metadata,
                                           &client_initial_metadata_)),
                   nullptr},
          [this](CallArgs call_args) -> ArenaPromise<ServerMetadataHandle> {
            return [this]() -> Poll<ServerMetadataHandle> {
              return CheckCompletion();
            };
          });
      Step();
    }

    void Orphan() override { abort(); }
    void ForceImmediateRepoll() override { abort(); }
    Waker MakeOwningWaker() override {
      return Waker(new WakeCall(main_loop_, id_));
    }
    Waker MakeNonOwningWaker() override {
      return Waker(new WakeCall(main_loop_, id_));
    }

    void RecvTrailingMetadata(const filter_fuzzer::Metadata& metadata) {
      if (server_trailing_metadata_ != nullptr) {
        LoadMetadata(metadata, &server_trailing_metadata_);
      }
      MakeOwningWaker().Wakeup();
    }

    void Wakeup() {
      ScopedContext context(this);
      Step();
    }

   private:
    class ScopedContext : public promise_detail::Context<Arena> {
     public:
      explicit ScopedContext(Call* call_data)
          : promise_detail::Context<Arena>(call_data->arena_.get()) {}
    };

    template <typename R>
    absl::optional<MetadataHandle<R>> LoadMetadata(
        const filter_fuzzer::Metadata& metadata, std::unique_ptr<R>* out) {
      if (*out != nullptr) return absl::nullopt;
      *out = absl::make_unique<R>(arena_.get());
      for (const auto& md : metadata.metadata()) {
        (*out)->Append(md.key(), Slice::FromCopiedString(md.value()),
                       [](absl::string_view, const Slice&) {});
      }
      return MetadataHandle<R>::TestOnlyWrap(out->get());
    }

    void Step() {
      if (!promise_.has_value()) return;
      auto r = (*promise_)();
      if (absl::holds_alternative<Pending>(r)) return;
      promise_.reset();
    }

    Poll<ServerMetadataHandle> CheckCompletion() {
      if (server_trailing_metadata_ != nullptr) {
        return ServerMetadataHandle::TestOnlyWrap(
            server_trailing_metadata_.get());
      }
      return Pending{};
    }

    MainLoop* const main_loop_;
    const uint32_t id_;
    ScopedArenaPtr arena_ = MakeScopedArena(32, &main_loop_->memory_allocator_);
    absl::optional<ArenaPromise<ServerMetadataHandle>> promise_;
    std::unique_ptr<ClientMetadata> client_initial_metadata_;
    std::unique_ptr<ServerMetadata> server_trailing_metadata_;
  };

  Call* GetCall(uint32_t id) {
    auto it = calls_.find(id);
    if (it == calls_.end()) return nullptr;
    return it->second.get();
  }

  MemoryAllocator memory_allocator_;
  std::unique_ptr<ChannelFilter> filter_;
  std::map<uint32_t, std::unique_ptr<Call>> calls_;
  std::vector<uint32_t> wakeups_;
};

}  // namespace
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const filter_fuzzer::Msg& msg) {
  grpc_core::GlobalObjects globals;
  auto channel_args = grpc_core::LoadChannelArgs(msg.channel_args(), &globals);
  auto filter = grpc_core::CreateFilter(msg.filter(), channel_args,
                                        grpc_core::ChannelFilter::Args());
  if (!filter.ok()) return;
  grpc_core::MainLoop main_loop(std::move(*filter), channel_args);
  for (const auto& action : msg.actions()) {
    main_loop.Run(action, &globals);
  }
}
