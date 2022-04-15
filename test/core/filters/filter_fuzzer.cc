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

#include "absl/memory/memory.h"

#include "src/core/ext/filters/channel_idle/channel_idle_filter.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/authorization/grpc_server_authz_filter.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/transport/transport_impl.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/filters/filter_fuzzer.pb.h"

bool squelch = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

static gpr_timespec g_now;
extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  gpr_timespec ts = g_now;
  ts.clock_type = clock_type;
  return ts;
}

namespace grpc_core {
namespace {

const grpc_transport_vtable kFakeTransportVTable = {
    // sizeof_stream
    0,
    // name
    "fake_transport",
    // init_stream
    [](grpc_transport*, grpc_stream*, grpc_stream_refcount*, const void*,
       Arena*) -> int { abort(); },
    // make_call_promise
    [](grpc_transport*,
       ClientMetadataHandle) -> ArenaPromise<ServerMetadataHandle> { abort(); },
    // set_pollset
    [](grpc_transport*, grpc_stream*, grpc_pollset*) { abort(); },
    // set_pollset_set
    [](grpc_transport*, grpc_stream*, grpc_pollset_set*) { abort(); },
    // perform_stream_op
    [](grpc_transport*, grpc_stream*, grpc_transport_stream_op_batch*) {
      abort();
    },
    // perform_op
    [](grpc_transport*, grpc_transport_op*) { abort(); },
    // destroy_stream
    [](grpc_transport*, grpc_stream*, grpc_closure*) { abort(); },
    // destroy
    [](grpc_transport*) { abort(); },
    // get_endpoint
    [](grpc_transport*) -> grpc_endpoint* { abort(); },
};

class FakeChannelSecurityConnector final
    : public grpc_channel_security_connector {
 public:
  FakeChannelSecurityConnector()
      : grpc_channel_security_connector("fake", nullptr, nullptr) {}

  void check_peer(tsi_peer, grpc_endpoint*, RefCountedPtr<grpc_auth_context>*,
                  grpc_closure*) override {
    abort();
  }

  void cancel_check_peer(grpc_closure*, grpc_error_handle) override { abort(); }

  int cmp(const grpc_security_connector*) const override { abort(); }

  ArenaPromise<absl::Status> CheckCallHost(absl::string_view,
                                           grpc_auth_context*) override {
    uint32_t qry = next_check_call_host_qry_++;
    return [this, qry]() -> Poll<absl::Status> {
      auto it = check_call_host_results_.find(qry);
      if (it == check_call_host_results_.end()) return Pending{};
      return it->second;
    };
  }

  void add_handshakers(const grpc_channel_args*, grpc_pollset_set*,
                       HandshakeManager*) override {
    abort();
  }

  void FinishCheckCallHost(uint32_t qry, absl::Status status) {
    check_call_host_results_.emplace(qry, std::move(status));
    check_call_host_wakers_[qry].Wakeup();
  }

 private:
  uint32_t next_check_call_host_qry_ = 0;
  std::map<uint32_t, Waker> check_call_host_wakers_;
  std::map<uint32_t, absl::Status> check_call_host_results_;
};

class ConstAuthorizationEngine final : public AuthorizationEngine {
 public:
  explicit ConstAuthorizationEngine(AuthorizationEngine::Decision decision)
      : decision_(decision) {}

  Decision Evaluate(const EvaluateArgs&) const override { return decision_; }

 private:
  Decision decision_;
};

class FakeAuthorizationPolicyProvider final
    : public grpc_authorization_policy_provider {
 public:
  explicit FakeAuthorizationPolicyProvider(AuthorizationEngines engines)
      : engines_(engines) {}
  void Orphan() override {}
  AuthorizationEngines engines() override { return engines_; }

 private:
  AuthorizationEngines engines_;
};

struct GlobalObjects {
  ResourceQuotaRefPtr resource_quota = MakeResourceQuota("test");
  grpc_transport transport{&kFakeTransportVTable};
  RefCountedPtr<FakeChannelSecurityConnector> channel_security_connector{
      MakeRefCounted<FakeChannelSecurityConnector>()};

  void Perform(const filter_fuzzer::GlobalObjectAction& action) {
    switch (action.type_case()) {
      case filter_fuzzer::GlobalObjectAction::TYPE_NOT_SET:
        break;
      case filter_fuzzer::GlobalObjectAction::kSetResourceQuota:
        resource_quota->memory_quota()->SetSize(action.set_resource_quota());
        break;
      case filter_fuzzer::GlobalObjectAction::kFinishCheckCallHost:
        channel_security_connector->FinishCheckCallHost(
            action.finish_check_call_host().qry(),
            absl::Status(
                absl::StatusCode(action.finish_check_call_host().status()),
                action.finish_check_call_host().message()));
        break;
    }
  }
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
          auto r = T::Create(channel_args, filter_args);
          if (!r.ok()) return r.status();
          return std::unique_ptr<ChannelFilter>(new T(std::move(*r)));
        }};
  }
};

RefCountedPtr<AuthorizationEngine> LoadAuthorizationEngine(
    const filter_fuzzer::AuthorizationEngine& engine) {
  switch (engine.engine_case()) {
    case filter_fuzzer::AuthorizationEngine::kAlwaysAllow:
      return MakeRefCounted<ConstAuthorizationEngine>(
          AuthorizationEngine::Decision{
              AuthorizationEngine::Decision::Type::kAllow,
              engine.always_allow()});
    case filter_fuzzer::AuthorizationEngine::kAlwaysDeny:
      return MakeRefCounted<ConstAuthorizationEngine>(
          AuthorizationEngine::Decision{
              AuthorizationEngine::Decision::Type::kDeny,
              engine.always_deny()});
    case filter_fuzzer::AuthorizationEngine::ENGINE_NOT_SET:
      break;
  }
  return MakeRefCounted<ConstAuthorizationEngine>(AuthorizationEngine::Decision{
      AuthorizationEngine::Decision::Type::kAllow, engine.always_allow()});
}

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
        args = args.SetObject(&globals->transport);
      }
    } else if (arg.key() == GRPC_ARG_SECURITY_CONNECTOR) {
      if (arg.value_case() ==
          filter_fuzzer::ChannelArg::kChannelSecurityConnector) {
        args = args.SetObject(globals->channel_security_connector);
      }
    } else if (arg.key() == GRPC_AUTH_CONTEXT_ARG) {
      if (arg.value_case() == filter_fuzzer::ChannelArg::kAuthContext) {
        args = args.SetObject(MakeRefCounted<grpc_auth_context>(nullptr));
      }
    } else if (arg.key() == GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER) {
      if (arg.value_case() ==
          filter_fuzzer::ChannelArg::kAuthorizationPolicyProvider) {
        args = args.SetObject(MakeRefCounted<FakeAuthorizationPolicyProvider>(
            grpc_authorization_policy_provider::AuthorizationEngines{
                LoadAuthorizationEngine(
                    arg.authorization_policy_provider().allow_engine()),
                LoadAuthorizationEngine(
                    arg.authorization_policy_provider().deny_engine())}));
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
        case filter_fuzzer::ChannelArg::kChannelSecurityConnector:
        case filter_fuzzer::ChannelArg::kAuthContext:
        case filter_fuzzer::ChannelArg::kAuthorizationPolicyProvider:
          break;
      }
    }
  }
  return args;
}

#define MAKE_FILTER(name) Filter::Make<name>(#name)

const Filter* const kFilters[] = {
    MAKE_FILTER(ClientAuthorityFilter), MAKE_FILTER(HttpClientFilter),
    MAKE_FILTER(ClientAuthFilter), MAKE_FILTER(GrpcServerAuthzFilter),
    // We exclude this one internally, so we can't have it here - will need to
    // pick it up through some future registration mechanism.
    // MAKE_FILTER(ServerLoadReportingFilter),
    // The following need channel stacks, and that's not figured out yet
    // MAKE_FILTER(MaxAgeFilter),
    // MAKE_FILTER(ClientIdleFilter),
};

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

  ~MainLoop() {
    ExecCtx exec_ctx;
    calls_.clear();
  }

  void Run(const filter_fuzzer::Action& action, GlobalObjects* globals) {
    ExecCtx exec_ctx;
    for (auto id : absl::exchange(wakeups_, {})) {
      if (auto* call = GetCall(id)) call->Wakeup();
    }
    switch (action.type_case()) {
      case filter_fuzzer::Action::TYPE_NOT_SET:
        break;
      case filter_fuzzer::Action::kAdvanceTimeMicroseconds:
        g_now = gpr_time_add(
            g_now, gpr_time_from_micros(action.advance_time_microseconds(),
                                        GPR_TIMESPAN));
        break;
      case filter_fuzzer::Action::kCancel:
        calls_.erase(action.call());
        break;
      case filter_fuzzer::Action::kCreateCall:
        calls_.emplace(
            action.call(),
            absl::make_unique<Call>(this, action.call(), action.create_call()));
        break;
      case filter_fuzzer::Action::kReceiveInitialMetadata:
        if (auto* call = GetCall(action.call())) {
          call->RecvInitialMetadata(action.receive_initial_metadata());
        }
        break;
      case filter_fuzzer::Action::kReceiveTrailingMetadata:
        if (auto* call = GetCall(action.call())) {
          call->RecvTrailingMetadata(action.receive_trailing_metadata());
        }
        break;
      case filter_fuzzer::Action::kSetFinalInfo:
        if (auto* call = GetCall(action.call())) {
          call->SetFinalInfo(action.set_final_info());
        }
        break;
      case filter_fuzzer::Action::kGlobalObjectAction:
        globals->Perform(action.global_object_action());
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
      auto* server_initial_metadata = arena_->New<Latch<ServerMetadata*>>();
      promise_ = main_loop_->filter_->MakeCallPromise(
          CallArgs{std::move(*LoadMetadata(client_initial_metadata,
                                           &client_initial_metadata_)),
                   server_initial_metadata},
          [this](CallArgs call_args) -> ArenaPromise<ServerMetadataHandle> {
            if (server_initial_metadata_) {
              call_args.server_initial_metadata->Set(
                  server_initial_metadata_.get());
            } else {
              unset_incoming_server_initial_metadata_latch_ =
                  call_args.server_initial_metadata;
            }
            return [this]() -> Poll<ServerMetadataHandle> {
              return CheckCompletion();
            };
          });
      Step();
    }

    ~Call() override {
      {
        ScopedContext context(this);
        // Don't pass final info thing if we were cancelled.
        if (promise_.has_value()) final_info_.reset();
        std::unique_ptr<grpc_call_final_info> final_info;
        if (final_info_) {
          final_info = absl::make_unique<grpc_call_final_info>();
          final_info->final_status =
              static_cast<grpc_status_code>(final_info_->status());
          final_info->error_string = final_info_->error_string().c_str();
          final_info->stats.latency =
              gpr_time_from_micros(final_info_->latency_us(), GPR_TIMESPAN);
          auto transport_stream_stats_from_proto =
              [](const filter_fuzzer::TransportOneWayStats& stats) {
                grpc_transport_one_way_stats s;
                s.framing_bytes = stats.framing_bytes();
                s.data_bytes = stats.data_bytes();
                s.header_bytes = stats.header_bytes();
                return s;
              };
          final_info->stats.transport_stream_stats.incoming =
              transport_stream_stats_from_proto(final_info_->incoming());
          final_info->stats.transport_stream_stats.outgoing =
              transport_stream_stats_from_proto(final_info_->outgoing());
        }
        finalization_.Run(final_info.get());
      }

      for (int i = 0; i < GRPC_CONTEXT_COUNT; i++) {
        if (legacy_context_[i].destroy != nullptr) {
          legacy_context_[i].destroy(legacy_context_[i].value);
        }
      }
    }

    void Orphan() override { abort(); }
    void ForceImmediateRepoll() override { context_->set_continue(); }
    Waker MakeOwningWaker() override {
      return Waker(new WakeCall(main_loop_, id_));
    }
    Waker MakeNonOwningWaker() override { return MakeOwningWaker(); }

    void RecvInitialMetadata(const filter_fuzzer::Metadata& metadata) {
      if (server_initial_metadata_ == nullptr) {
        LoadMetadata(metadata, &server_initial_metadata_);
        if (auto* latch = absl::exchange(
                unset_incoming_server_initial_metadata_latch_, nullptr)) {
          ScopedContext context(this);
          latch->Set(server_initial_metadata_.get());
        }
      }
    }

    void RecvTrailingMetadata(const filter_fuzzer::Metadata& metadata) {
      if (server_trailing_metadata_ == nullptr) {
        LoadMetadata(metadata, &server_trailing_metadata_);
        server_trailing_metadata_waker_.Wakeup();
      }
    }

    void Wakeup() {
      ScopedContext context(this);
      Step();
    }

    void SetFinalInfo(filter_fuzzer::FinalInfo final_info) {
      final_info_ = absl::make_unique<filter_fuzzer::FinalInfo>(final_info);
    }

   private:
    class ScopedContext
        : public ScopedActivity,
          public promise_detail::Context<Arena>,
          public promise_detail::Context<grpc_call_context_element>,
          public promise_detail::Context<CallFinalization> {
     public:
      explicit ScopedContext(Call* call)
          : ScopedActivity(call),
            promise_detail::Context<Arena>(call->arena_.get()),
            promise_detail::Context<grpc_call_context_element>(
                call->legacy_context_),
            promise_detail::Context<CallFinalization>(&call->finalization_),
            call_(call) {
        GPR_ASSERT(call_->context_ == nullptr);
        call_->context_ = this;
      }
      ~ScopedContext() {
        while (bool step = absl::exchange(continue_, false)) {
          call_->Step();
        }
        GPR_ASSERT(call_->context_ == this);
        call_->context_ = nullptr;
      }

      void set_continue() { continue_ = true; }

     private:
      Call* const call_;
      bool continue_ = false;
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
      ServerMetadataHandle md = std::move(absl::get<ServerMetadataHandle>(r));
      if (md.get() != server_trailing_metadata_.get()) md->~ServerMetadata();
      promise_.reset();
    }

    Poll<ServerMetadataHandle> CheckCompletion() {
      if (server_trailing_metadata_ != nullptr) {
        return ServerMetadataHandle::TestOnlyWrap(
            server_trailing_metadata_.get());
      }
      server_trailing_metadata_waker_ = MakeOwningWaker();
      return Pending{};
    }

    MainLoop* const main_loop_;
    const uint32_t id_;
    ScopedArenaPtr arena_ = MakeScopedArena(32, &main_loop_->memory_allocator_);
    absl::optional<ArenaPromise<ServerMetadataHandle>> promise_;
    std::unique_ptr<filter_fuzzer::FinalInfo> final_info_;
    std::unique_ptr<ClientMetadata> client_initial_metadata_;
    std::unique_ptr<ServerMetadata> server_initial_metadata_;
    Latch<ServerMetadata*>* unset_incoming_server_initial_metadata_latch_ =
        nullptr;
    std::unique_ptr<ServerMetadata> server_trailing_metadata_;
    Waker server_trailing_metadata_waker_;
    CallFinalization finalization_;
    ScopedContext* context_ = nullptr;
    grpc_call_context_element legacy_context_[GRPC_CONTEXT_COUNT] = {};
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
  grpc_test_only_set_slice_hash_seed(0);
  char* grpc_trace_fuzzer = gpr_getenv("GRPC_TRACE_FUZZER");
  if (squelch && grpc_trace_fuzzer == nullptr) gpr_set_log_function(dont_log);
  gpr_free(grpc_trace_fuzzer);
  g_now = {1, 0, GPR_CLOCK_MONOTONIC};
  grpc_core::TestOnlySetProcessEpoch(g_now);
  gpr_now_impl = now_impl;
  grpc_init();
  grpc_timer_manager_set_threading(false);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  }

  grpc_core::GlobalObjects globals;
  auto channel_args = grpc_core::LoadChannelArgs(msg.channel_args(), &globals);
  auto filter = grpc_core::CreateFilter(msg.filter(), channel_args,
                                        grpc_core::ChannelFilter::Args());
  if (filter.ok()) {
    grpc_core::MainLoop main_loop(std::move(*filter), channel_args);
    for (const auto& action : msg.actions()) {
      grpc_timer_manager_tick();
      main_loop.Run(action, &globals);
    }
  }

  grpc_shutdown();
}
