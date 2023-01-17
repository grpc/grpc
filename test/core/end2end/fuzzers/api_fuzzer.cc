//
//
// Copyright 2016 gRPC authors.
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

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/passthru_endpoint.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

// Applicable when simulating channel actions. Prevents overflows.
static constexpr uint64_t kMaxWaitMs = grpc_core::Duration::Hours(1).millis();
// Applicable when simulating channel actions. Prevents overflows.
static constexpr uint64_t kMaxAddNReadableBytes = (2 * 1024 * 1024);  // 2GB
// Applicable when simulating channel actions. Prevents overflows.
static constexpr uint64_t kMaxAddNWritableBytes = (2 * 1024 * 1024);  // 2GB

////////////////////////////////////////////////////////////////////////////////
// logging

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

////////////////////////////////////////////////////////////////////////////////
// global state

static grpc_server* g_server;
static grpc_channel* g_channel;
static grpc_resource_quota* g_resource_quota;
static std::vector<grpc_passthru_endpoint_channel_action> g_channel_actions;
static std::atomic<bool> g_channel_force_delete{false};

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  char* addr;
  grpc_closure* on_done;
  std::unique_ptr<grpc_core::ServerAddressList>* addresses;
} addr_req;

static void finish_resolve(addr_req r) {
  if (0 == strcmp(r.addr, "server")) {
    *r.addresses = std::make_unique<grpc_core::ServerAddressList>();
    grpc_resolved_address fake_resolved_address;
    GPR_ASSERT(
        grpc_parse_ipv4_hostport("1.2.3.4:5", &fake_resolved_address, false));
    (*r.addresses)
        ->emplace_back(fake_resolved_address, grpc_core::ChannelArgs());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r.on_done, absl::OkStatus());
  } else {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r.on_done,
                            absl::UnknownError("Resolution failed"));
  }
  gpr_free(r.addr);
}

namespace {

using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

class FuzzerDNSResolver : public grpc_core::DNSResolver {
 public:
  class FuzzerDNSRequest {
   public:
    FuzzerDNSRequest(
        absl::string_view name,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_done)
        : name_(std::string(name)), on_done_(std::move(on_done)) {
      GetDefaultEventEngine()->RunAfter(
          grpc_core::Duration::Seconds(1), [this] {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            FinishResolve();
          });
    }

   private:
    void FinishResolve() {
      if (name_ == "server") {
        std::vector<grpc_resolved_address> addrs;
        grpc_resolved_address addr;
        addr.len = 0;
        addrs.push_back(addr);
        on_done_(std::move(addrs));
      } else {
        on_done_(absl::UnknownError("Resolution failed"));
      }
      delete this;
    }

    const std::string name_;
    const std::function<void(
        absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done_;
  };

  explicit FuzzerDNSResolver(FuzzingEventEngine* engine) : engine_(engine) {}

  TaskHandle LookupHostname(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, absl::string_view /* default_port */,
      grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    new FuzzerDNSRequest(name, std::move(on_resolved));
    return kNullHandle;
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> LookupHostnameBlocking(
      absl::string_view /* name */,
      absl::string_view /* default_port */) override {
    GPR_ASSERT(0);
  }

  TaskHandle LookupSRV(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    engine_->Run([on_resolved] {
      grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Fuzzing DNS resolver does not support looking up SRV records"));
    });
    return {-1, -1};
  };

  TaskHandle LookupTXT(
      std::function<void(absl::StatusOr<std::string>)> on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    // Not supported
    engine_->Run([on_resolved] {
      grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Fuzing DNS resolver does not support looking up TXT records"));
    });
    return {-1, -1};
  };

  // FuzzerDNSResolver does not support cancellation.
  bool Cancel(TaskHandle /*handle*/) override { return false; }

 private:
  FuzzingEventEngine* engine_;
};

}  // namespace

grpc_ares_request* my_dns_lookup_ares(
    const char* /*dns_server*/, const char* addr, const char* /*default_port*/,
    grpc_pollset_set* /*interested_parties*/, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addresses,
    int /*query_timeout*/) {
  addr_req r;
  r.addr = gpr_strdup(addr);
  r.on_done = on_done;
  r.addresses = addresses;
  GetDefaultEventEngine()->RunAfter(grpc_core::Duration::Seconds(1), [r] {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    finish_resolve(r);
  });
  return nullptr;
}

static void my_cancel_ares_request(grpc_ares_request* request) {
  GPR_ASSERT(request == nullptr);
}
////////////////////////////////////////////////////////////////////////////////
// client connection

static void sched_connect(grpc_closure* closure, grpc_endpoint** ep,
                          gpr_timespec deadline);

typedef struct {
  grpc_closure* closure;
  grpc_endpoint** ep;
  gpr_timespec deadline;
} future_connect;

static void do_connect(future_connect fc) {
  if (g_server != nullptr) {
    grpc_endpoint* client;
    grpc_endpoint* server;
    grpc_passthru_endpoint_create(&client, &server, nullptr, true);
    *fc.ep = client;
    start_scheduling_grpc_passthru_endpoint_channel_effects(client,
                                                            g_channel_actions);

    grpc_core::Server* core_server = grpc_core::Server::FromC(g_server);
    grpc_transport* transport = grpc_create_chttp2_transport(
        core_server->channel_args(), server, false);
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "SetupTransport",
        core_server->SetupTransport(transport, nullptr,
                                    core_server->channel_args(), nullptr)));
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, fc.closure, absl::OkStatus());
  } else {
    sched_connect(fc.closure, fc.ep, fc.deadline);
  }
}

static void sched_connect(grpc_closure* closure, grpc_endpoint** ep,
                          gpr_timespec deadline) {
  if (gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) < 0) {
    *ep = nullptr;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure,
                            GRPC_ERROR_CREATE("Connect deadline exceeded"));
    return;
  }
  future_connect fc;
  fc.closure = closure;
  fc.ep = ep;
  fc.deadline = deadline;
  GetDefaultEventEngine()->RunAfter(grpc_core::Duration::Seconds(1), [fc] {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
    grpc_core::ExecCtx exec_ctx;
    do_connect(fc);
  });
}

static int64_t my_tcp_client_connect(
    grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* /*interested_parties*/,
    const grpc_event_engine::experimental::EndpointConfig& /*config*/,
    const grpc_resolved_address* /*addr*/, grpc_core::Timestamp deadline) {
  sched_connect(closure, ep, deadline.as_timespec(GPR_CLOCK_MONOTONIC));
  return 0;
}

static bool my_tcp_cancel_connect(int64_t /*connection_handle*/) {
  return false;
}

grpc_tcp_client_vtable fuzz_tcp_client_vtable = {my_tcp_client_connect,
                                                 my_tcp_cancel_connect};

////////////////////////////////////////////////////////////////////////////////
// test driver

class Validator {
 public:
  explicit Validator(std::function<void(bool)> impl) : impl_(std::move(impl)) {}

  virtual ~Validator() {}
  void Run(bool success) {
    impl_(success);
    delete this;
  }

 private:
  std::function<void(bool)> impl_;
};

Validator* MakeValidator(std::function<void(bool)> impl) {
  return new Validator(std::move(impl));
}

static Validator* AssertSuccessAndDecrement(int* counter) {
  return MakeValidator([counter](bool success) {
    GPR_ASSERT(success);
    --*counter;
  });
}

static Validator* Decrement(int* counter) {
  return MakeValidator([counter](bool) { --*counter; });
}

static Validator* ValidateConnectivityWatch(gpr_timespec deadline,
                                            int* counter) {
  return MakeValidator([deadline, counter](bool success) {
    if (!success) {
      auto now = gpr_now(deadline.clock_type);
      GPR_ASSERT(gpr_time_cmp(now, deadline) >= 0);
    }
    --*counter;
  });
}

static void free_non_null(void* p) {
  GPR_ASSERT(p != nullptr);
  gpr_free(p);
}

enum class CallType { CLIENT, SERVER, PENDING_SERVER, TOMBSTONED };

class Call : public std::enable_shared_from_this<Call> {
 public:
  explicit Call(CallType type) : type_(type) {
    grpc_metadata_array_init(&recv_initial_metadata_);
    grpc_metadata_array_init(&recv_trailing_metadata_);
    grpc_call_details_init(&call_details_);
  }

  ~Call();

  CallType type() const { return type_; }

  bool done() const {
    if ((type_ == CallType::TOMBSTONED || call_closed_) && pending_ops_ == 0) {
      return true;
    }
    if (call_ == nullptr && type() != CallType::PENDING_SERVER) return true;
    return false;
  }

  void Shutdown() {
    if (call_ != nullptr) {
      grpc_call_cancel(call_, nullptr);
      type_ = CallType::TOMBSTONED;
    }
  }

  void SetCall(grpc_call* call) {
    GPR_ASSERT(call_ == nullptr);
    call_ = call;
  }

  grpc_call* call() const { return call_; }

  void RequestCall(grpc_server* server, grpc_completion_queue* cq) {
    auto* v = FinishedRequestCall();
    grpc_call_error error = grpc_server_request_call(
        server, &call_, &call_details_, &recv_initial_metadata_, cq, cq, v);
    if (error != GRPC_CALL_OK) {
      v->Run(false);
    }
  }

  void* Allocate(size_t size) {
    void* p = gpr_malloc(size);
    free_pointers_.push_back(p);
    return p;
  }

  template <typename T>
  T* AllocArray(size_t elems) {
    return static_cast<T*>(Allocate(sizeof(T) * elems));
  }

  template <typename T>
  T* NewCopy(T value) {
    T* p = AllocArray<T>(1);
    new (p) T(value);
    return p;
  }

  template <typename T>
  grpc_slice ReadSlice(const T& s) {
    grpc_slice slice = grpc_slice_from_cpp_string(s.value());
    unref_slices_.push_back(slice);
    return slice;
  }

  template <typename M>
  grpc_metadata_array ReadMetadata(const M& metadata) {
    grpc_metadata* m = AllocArray<grpc_metadata>(metadata.size());
    for (int i = 0; i < metadata.size(); ++i) {
      m[i].key = ReadSlice(metadata[i].key());
      m[i].value = ReadSlice(metadata[i].value());
    }
    return grpc_metadata_array{static_cast<size_t>(metadata.size()),
                               static_cast<size_t>(metadata.size()), m};
  }

  absl::optional<grpc_op> ReadOp(
      const api_fuzzer::BatchOp& batch_op, bool* batch_is_ok,
      uint8_t* batch_ops, std::vector<std::function<void()>>* unwinders) {
    grpc_op op;
    memset(&op, 0, sizeof(op));
    switch (batch_op.op_case()) {
      case api_fuzzer::BatchOp::OP_NOT_SET:
        // invalid value
        return {};
      case api_fuzzer::BatchOp::kSendInitialMetadata:
        if (sent_initial_metadata_) {
          *batch_is_ok = false;
        } else {
          sent_initial_metadata_ = true;
          op.op = GRPC_OP_SEND_INITIAL_METADATA;
          *batch_ops |= 1 << GRPC_OP_SEND_INITIAL_METADATA;
          auto ary = ReadMetadata(batch_op.send_initial_metadata().metadata());
          op.data.send_initial_metadata.count = ary.count;
          op.data.send_initial_metadata.metadata = ary.metadata;
        }
        break;
      case api_fuzzer::BatchOp::kSendMessage:
        op.op = GRPC_OP_SEND_MESSAGE;
        if (send_message_ != nullptr) {
          *batch_is_ok = false;
        } else {
          *batch_ops |= 1 << GRPC_OP_SEND_MESSAGE;
          std::vector<grpc_slice> slices;
          for (const auto& m : batch_op.send_message().message()) {
            slices.push_back(ReadSlice(m));
          }
          send_message_ = op.data.send_message.send_message =
              grpc_raw_byte_buffer_create(slices.data(), slices.size());
          unwinders->push_back([this]() {
            grpc_byte_buffer_destroy(send_message_);
            send_message_ = nullptr;
          });
        }
        break;
      case api_fuzzer::BatchOp::kSendCloseFromClient:
        op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
        *batch_ops |= 1 << GRPC_OP_SEND_CLOSE_FROM_CLIENT;
        break;
      case api_fuzzer::BatchOp::kSendStatusFromServer: {
        op.op = GRPC_OP_SEND_STATUS_FROM_SERVER;
        *batch_ops |= 1 << GRPC_OP_SEND_STATUS_FROM_SERVER;
        auto ary = ReadMetadata(batch_op.send_status_from_server().metadata());
        op.data.send_status_from_server.trailing_metadata_count = ary.count;
        op.data.send_status_from_server.trailing_metadata = ary.metadata;
        op.data.send_status_from_server.status = static_cast<grpc_status_code>(
            batch_op.send_status_from_server().status_code());
        op.data.send_status_from_server.status_details =
            batch_op.send_status_from_server().has_status_details()
                ? NewCopy(ReadSlice(
                      batch_op.send_status_from_server().status_details()))
                : nullptr;
      } break;
      case api_fuzzer::BatchOp::kReceiveInitialMetadata:
        if (enqueued_recv_initial_metadata_) {
          *batch_is_ok = false;
        } else {
          enqueued_recv_initial_metadata_ = true;
          op.op = GRPC_OP_RECV_INITIAL_METADATA;
          *batch_ops |= 1 << GRPC_OP_RECV_INITIAL_METADATA;
          op.data.recv_initial_metadata.recv_initial_metadata =
              &recv_initial_metadata_;
        }
        break;
      case api_fuzzer::BatchOp::kReceiveMessage:
        // Allow only one active pending_recv_message_op to exist. Otherwise if
        // the previous enqueued recv_message_op is not complete by the time
        // we get here, then under certain conditions, enqueing this op will
        // over-write the internal call->receiving_buffer maintained by grpc
        // leading to a memory leak.
        if (call_closed_ || pending_recv_message_op_) {
          *batch_is_ok = false;
        } else {
          op.op = GRPC_OP_RECV_MESSAGE;
          *batch_ops |= 1 << GRPC_OP_RECV_MESSAGE;
          pending_recv_message_op_ = true;
          op.data.recv_message.recv_message = &recv_message_;
          unwinders->push_back([this]() { pending_recv_message_op_ = false; });
        }
        break;
      case api_fuzzer::BatchOp::kReceiveStatusOnClient:
        op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
        op.data.recv_status_on_client.status = &status_;
        op.data.recv_status_on_client.trailing_metadata =
            &recv_trailing_metadata_;
        op.data.recv_status_on_client.status_details = &recv_status_details_;
        *batch_ops |= 1 << GRPC_OP_RECV_STATUS_ON_CLIENT;
        break;
      case api_fuzzer::BatchOp::kReceiveCloseOnServer:
        op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
        *batch_ops |= 1 << GRPC_OP_RECV_CLOSE_ON_SERVER;
        op.data.recv_close_on_server.cancelled = &cancelled_;
        break;
    }
    op.reserved = nullptr;
    op.flags = batch_op.flags();
    return op;
  }

  Validator* FinishedBatchValidator(uint8_t has_ops) {
    ++pending_ops_;
    auto self = shared_from_this();
    return MakeValidator([self, has_ops](bool /*success*/) {
      --self->pending_ops_;
      if (has_ops & (1u << GRPC_OP_RECV_MESSAGE)) {
        self->pending_recv_message_op_ = false;
        if (self->recv_message_ != nullptr) {
          grpc_byte_buffer_destroy(self->recv_message_);
          self->recv_message_ = nullptr;
        }
      }
      if ((has_ops & (1u << GRPC_OP_SEND_MESSAGE))) {
        grpc_byte_buffer_destroy(self->send_message_);
        self->send_message_ = nullptr;
      }
      if ((has_ops & (1u << GRPC_OP_RECV_STATUS_ON_CLIENT)) ||
          (has_ops & (1u << GRPC_OP_RECV_CLOSE_ON_SERVER))) {
        self->call_closed_ = true;
      }
    });
  }

  Validator* FinishedRequestCall() {
    ++pending_ops_;
    auto self = shared_from_this();
    return MakeValidator([self](bool success) {
      GPR_ASSERT(self->pending_ops_ > 0);
      --self->pending_ops_;
      if (success) {
        GPR_ASSERT(self->call_ != nullptr);
        self->type_ = CallType::SERVER;
      } else {
        self->type_ = CallType::TOMBSTONED;
      }
    });
  }

 private:
  CallType type_;
  grpc_call* call_ = nullptr;
  grpc_byte_buffer* recv_message_ = nullptr;
  grpc_status_code status_;
  grpc_metadata_array recv_initial_metadata_{0, 0, nullptr};
  grpc_metadata_array recv_trailing_metadata_{0, 0, nullptr};
  grpc_slice recv_status_details_ = grpc_empty_slice();
  // set by receive close on server, unset here to trigger
  // msan if misused
  int cancelled_;
  int pending_ops_ = 0;
  bool sent_initial_metadata_ = false;
  bool enqueued_recv_initial_metadata_ = false;
  grpc_call_details call_details_{};
  grpc_byte_buffer* send_message_ = nullptr;
  bool call_closed_ = false;
  bool pending_recv_message_op_ = false;

  std::vector<void*> free_pointers_;
  std::vector<grpc_slice> unref_slices_;
};

static std::vector<std::shared_ptr<Call>> g_calls;
static size_t g_active_call = 0;

static Call* ActiveCall() {
  while (!g_calls.empty()) {
    if (g_active_call >= g_calls.size()) {
      g_active_call = 0;
    }
    if (g_calls[g_active_call] != nullptr && !g_calls[g_active_call]->done()) {
      return g_calls[g_active_call].get();
    }
    g_calls.erase(g_calls.begin() + g_active_call);
  }
  return nullptr;
}

Call::~Call() {
  if (call_ != nullptr) {
    grpc_call_unref(call_);
  }
  grpc_slice_unref(recv_status_details_);
  grpc_call_details_destroy(&call_details_);

  for (auto* p : free_pointers_) {
    gpr_free(p);
  }
  for (auto s : unref_slices_) {
    grpc_slice_unref(s);
  }

  if (recv_message_ != nullptr) {
    grpc_byte_buffer_destroy(recv_message_);
    recv_message_ = nullptr;
  }

  grpc_metadata_array_destroy(&recv_initial_metadata_);
  grpc_metadata_array_destroy(&recv_trailing_metadata_);
}

template <typename ChannelArgContainer>
grpc_channel_args* ReadArgs(const ChannelArgContainer& args) {
  grpc_channel_args* res =
      static_cast<grpc_channel_args*>(gpr_malloc(sizeof(grpc_channel_args)));
  res->args =
      static_cast<grpc_arg*>(gpr_malloc(sizeof(grpc_arg) * args.size()));
  int j = 0;
  for (int i = 0; i < args.size(); i++) {
    switch (args[i].value_case()) {
      case api_fuzzer::ChannelArg::kStr:
        res->args[j].type = GRPC_ARG_STRING;
        res->args[j].value.string = gpr_strdup(args[i].str().c_str());
        break;
      case api_fuzzer::ChannelArg::kI:
        res->args[j].type = GRPC_ARG_INTEGER;
        res->args[j].value.integer = args[i].i();
        break;
      case api_fuzzer::ChannelArg::kResourceQuota:
        if (args[i].key() != GRPC_ARG_RESOURCE_QUOTA) continue;
        grpc_resource_quota_ref(g_resource_quota);
        res->args[j].type = GRPC_ARG_POINTER;
        res->args[j].value.pointer.p = g_resource_quota;
        res->args[j].value.pointer.vtable = grpc_resource_quota_arg_vtable();
        break;
      case api_fuzzer::ChannelArg::VALUE_NOT_SET:
        res->args[j].type = GRPC_ARG_INTEGER;
        res->args[j].value.integer = 0;
        break;
    }
    res->args[j].key = gpr_strdup(args[i].key().c_str());
    ++j;
  }
  res->num_args = j;
  return res;
}

static const char* ReadCredArtifact(
    const api_fuzzer::CredArtifact& artifact,
    std::initializer_list<const char*> builtins) {
  switch (artifact.type_case()) {
    case api_fuzzer::CredArtifact::kCustom:
      return artifact.custom().c_str();
    case api_fuzzer::CredArtifact::kBuiltin:
      if (artifact.builtin() < 0) return nullptr;
      if (artifact.builtin() < static_cast<int>(builtins.size())) {
        return *(builtins.begin() + artifact.builtin());
      }
      return nullptr;
    case api_fuzzer::CredArtifact::TYPE_NOT_SET:
      return nullptr;
  }
}

static grpc_channel_credentials* ReadSslChannelCreds(
    const api_fuzzer::SslChannelCreds& creds) {
  const char* root_certs =
      creds.has_root_certs()
          ? ReadCredArtifact(creds.root_certs(), {test_root_cert})
          : nullptr;
  const char* private_key =
      creds.has_private_key()
          ? ReadCredArtifact(creds.private_key(),
                             {test_server1_key, test_self_signed_client_key,
                              test_signed_client_key})
          : nullptr;
  const char* certs =
      creds.has_certs()
          ? ReadCredArtifact(creds.certs(),
                             {test_server1_cert, test_self_signed_client_cert,
                              test_signed_client_cert})
          : nullptr;
  grpc_ssl_pem_key_cert_pair key_cert_pair = {private_key, certs};
  return grpc_ssl_credentials_create(
      root_certs,
      private_key != nullptr && certs != nullptr ? &key_cert_pair : nullptr,
      nullptr, nullptr);
}

static grpc_call_credentials* ReadCallCreds(
    const api_fuzzer::CallCreds& creds) {
  switch (creds.type_case()) {
    case api_fuzzer::CallCreds::TYPE_NOT_SET:
      return nullptr;
    case api_fuzzer::CallCreds::kNull:
      return nullptr;
    case api_fuzzer::CallCreds::kCompositeCallCreds: {
      grpc_call_credentials* out = nullptr;
      for (const auto& child_creds :
           creds.composite_call_creds().call_creds()) {
        grpc_call_credentials* child = ReadCallCreds(child_creds);
        if (child != nullptr) {
          if (out == nullptr) {
            out = child;
          } else {
            auto* composed =
                grpc_composite_call_credentials_create(out, child, nullptr);
            grpc_call_credentials_release(child);
            grpc_call_credentials_release(out);
            out = composed;
          }
        }
      }
      return out;
    }
    case api_fuzzer::CallCreds::kAccessToken:
      return grpc_access_token_credentials_create(creds.access_token().c_str(),
                                                  nullptr);
    case api_fuzzer::CallCreds::kIam:
      return grpc_google_iam_credentials_create(
          creds.iam().auth_token().c_str(), creds.iam().auth_selector().c_str(),
          nullptr);
      // TODO(ctiller): more cred types here
  }
}

static grpc_channel_credentials* ReadChannelCreds(
    const api_fuzzer::ChannelCreds& creds) {
  switch (creds.type_case()) {
    case api_fuzzer::ChannelCreds::TYPE_NOT_SET:
      return nullptr;
    case api_fuzzer::ChannelCreds::kSslChannelCreds:
      return ReadSslChannelCreds(creds.ssl_channel_creds());
    case api_fuzzer::ChannelCreds::kCompositeChannelCreds: {
      const auto& comp = creds.composite_channel_creds();
      grpc_channel_credentials* c1 =
          comp.has_channel_creds() ? ReadChannelCreds(comp.channel_creds())
                                   : nullptr;
      grpc_call_credentials* c2 =
          comp.has_call_creds() ? ReadCallCreds(comp.call_creds()) : nullptr;
      if (c1 != nullptr && c2 != nullptr) {
        grpc_channel_credentials* out =
            grpc_composite_channel_credentials_create(c1, c2, nullptr);
        grpc_channel_credentials_release(c1);
        grpc_call_credentials_release(c2);
        return out;
      } else if (c1 != nullptr) {
        return c1;
      } else if (c2 != nullptr) {
        grpc_call_credentials_release(c2);
        return nullptr;
      } else {
        return nullptr;
      }
      GPR_UNREACHABLE_CODE(return nullptr);
    }
    case api_fuzzer::ChannelCreds::kNull:
      return nullptr;
  }
}

DEFINE_PROTO_FUZZER(const api_fuzzer::Msg& msg) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  grpc_event_engine::experimental::SetEventEngineFactory(
      [actions = msg.event_engine_actions()]() {
        return std::make_unique<FuzzingEventEngine>(
            FuzzingEventEngine::Options(), actions);
      });
  auto engine =
      std::dynamic_pointer_cast<FuzzingEventEngine>(GetDefaultEventEngine());
  FuzzingEventEngine::SetGlobalNowImplEngine(engine.get());
  grpc_init();
  grpc_set_tcp_client_impl(&fuzz_tcp_client_vtable);
  grpc_timer_manager_set_threading(false);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  }
  grpc_core::ResetDNSResolver(
      std::make_unique<FuzzerDNSResolver>(engine.get()));
  grpc_dns_lookup_hostname_ares = my_dns_lookup_ares;
  grpc_cancel_ares_request = my_cancel_ares_request;

  GPR_ASSERT(g_channel == nullptr);
  GPR_ASSERT(g_server == nullptr);

  bool server_shutdown = false;
  int pending_server_shutdowns = 0;
  int pending_channel_watches = 0;
  int pending_pings = 0;

  g_resource_quota = grpc_resource_quota_create("api_fuzzer");

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);

  int action_index = 0;
  auto no_more_actions = [&]() { action_index = msg.actions_size(); };
  auto poll_cq = [&]() -> bool {
    grpc_event ev = grpc_completion_queue_next(
        cq, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
    switch (ev.type) {
      case GRPC_OP_COMPLETE: {
        static_cast<Validator*>(ev.tag)->Run(ev.success);
        break;
      }
      case GRPC_QUEUE_TIMEOUT:
        break;
      case GRPC_QUEUE_SHUTDOWN:
        return true;
    }
    return false;
  };

  while (action_index < msg.actions_size() || g_channel != nullptr ||
         g_server != nullptr || pending_channel_watches > 0 ||
         pending_pings > 0 || ActiveCall() != nullptr) {
    engine->Tick();

    if (action_index == msg.actions_size()) {
      engine->FuzzingDone();
      if (g_channel != nullptr) {
        grpc_channel_destroy(g_channel);
        g_channel = nullptr;
      }
      if (g_server != nullptr) {
        if (!server_shutdown) {
          grpc_server_shutdown_and_notify(
              g_server, cq,
              AssertSuccessAndDecrement(&pending_server_shutdowns));
          server_shutdown = true;
          pending_server_shutdowns++;
        } else if (pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = nullptr;
        }
      }
      for (auto& call : g_calls) {
        if (call == nullptr) continue;
        if (call->type() == CallType::PENDING_SERVER) continue;
        call->Shutdown();
      }

      grpc_timer_manager_tick();
      GPR_ASSERT(!poll_cq());
      continue;
    }

    grpc_timer_manager_tick();

    if (g_channel_force_delete.exchange(false) && g_channel) {
      grpc_channel_destroy(g_channel);
      g_channel = nullptr;
      g_channel_actions.clear();
    }

    const api_fuzzer::Action& action = msg.actions(action_index);
    action_index++;
    switch (action.type_case()) {
      case api_fuzzer::Action::TYPE_NOT_SET:
        no_more_actions();
        break;
      // tickle completion queue
      case api_fuzzer::Action::kPollCq: {
        GPR_ASSERT(!poll_cq());
        break;
      }
      // create an insecure channel
      case api_fuzzer::Action::kCreateChannel: {
        if (!action.create_channel().channel_actions_size() ||
            g_channel != nullptr) {
          no_more_actions();
        } else {
          grpc_channel_args* args =
              ReadArgs(action.create_channel().channel_args());
          grpc_channel_credentials* creds =
              action.create_channel().has_channel_creds()
                  ? ReadChannelCreds(action.create_channel().channel_creds())
                  : grpc_insecure_credentials_create();
          g_channel = grpc_channel_create(
              action.create_channel().target().c_str(), creds, args);
          grpc_channel_credentials_release(creds);
          g_channel_actions.clear();
          for (int i = 0; i < action.create_channel().channel_actions_size();
               i++) {
            const api_fuzzer::ChannelAction& channel_action =
                action.create_channel().channel_actions(i);
            g_channel_actions.push_back({
                std::min(channel_action.wait_ms(), kMaxWaitMs),
                std::min(channel_action.add_n_bytes_writable(),
                         kMaxAddNWritableBytes),
                std::min(channel_action.add_n_bytes_readable(),
                         kMaxAddNReadableBytes),
            });
          }
          GPR_ASSERT(g_channel != nullptr);
          g_channel_force_delete = false;
          {
            grpc_core::ExecCtx exec_ctx;
            grpc_channel_args_destroy(args);
          }
        }
        break;
      }
      // destroy a channel
      case api_fuzzer::Action::kCloseChannel: {
        if (g_channel != nullptr) {
          grpc_channel_destroy(g_channel);
          g_channel = nullptr;
        } else {
          no_more_actions();
        }
        break;
      }
      // bring up a server
      case api_fuzzer::Action::kCreateServer: {
        if (g_server == nullptr) {
          grpc_channel_args* args =
              ReadArgs(action.create_server().channel_args());
          g_server = grpc_server_create(args, nullptr);
          GPR_ASSERT(g_server != nullptr);
          {
            grpc_core::ExecCtx exec_ctx;
            grpc_channel_args_destroy(args);
          }
          grpc_server_register_completion_queue(g_server, cq, nullptr);
          grpc_server_start(g_server);
          server_shutdown = false;
          GPR_ASSERT(pending_server_shutdowns == 0);
        } else {
          no_more_actions();
        }
        break;
      }
      // begin server shutdown
      case api_fuzzer::Action::kShutdownServer: {
        if (g_server != nullptr) {
          grpc_server_shutdown_and_notify(
              g_server, cq,
              AssertSuccessAndDecrement(&pending_server_shutdowns));
          pending_server_shutdowns++;
          server_shutdown = true;
        } else {
          no_more_actions();
        }
        break;
      }
      // cancel all calls if shutdown
      case api_fuzzer::Action::kCancelAllCallsIfShutdown: {
        if (g_server != nullptr && server_shutdown) {
          grpc_server_cancel_all_calls(g_server);
        } else {
          no_more_actions();
        }
        break;
      }
      // destroy server
      case api_fuzzer::Action::kDestroyServerIfReady: {
        if (g_server != nullptr && server_shutdown &&
            pending_server_shutdowns == 0) {
          grpc_server_destroy(g_server);
          g_server = nullptr;
        } else {
          no_more_actions();
        }
        break;
      }
      // check connectivity
      case api_fuzzer::Action::kCheckConnectivity: {
        if (g_channel != nullptr) {
          grpc_channel_check_connectivity_state(g_channel,
                                                action.check_connectivity());
        } else {
          no_more_actions();
        }
        break;
      }
      // watch connectivity
      case api_fuzzer::Action::kWatchConnectivity: {
        if (g_channel != nullptr) {
          grpc_connectivity_state st =
              grpc_channel_check_connectivity_state(g_channel, 0);
          if (st != GRPC_CHANNEL_SHUTDOWN) {
            gpr_timespec deadline =
                gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                             gpr_time_from_micros(action.watch_connectivity(),
                                                  GPR_TIMESPAN));
            grpc_channel_watch_connectivity_state(
                g_channel, st, deadline, cq,
                ValidateConnectivityWatch(deadline, &pending_channel_watches));
            pending_channel_watches++;
          }
        } else {
          no_more_actions();
        }
        break;
      }
      // create a call
      case api_fuzzer::Action::kCreateCall: {
        bool ok = true;
        if (g_channel == nullptr) ok = false;
        // If the active call is a server call, then use it as the parent call
        // to exercise the propagation logic.
        Call* parent_call = ActiveCall();
        if (parent_call != nullptr && parent_call->type() != CallType::SERVER) {
          parent_call = nullptr;
        }
        g_calls.emplace_back(new Call(CallType::CLIENT));
        grpc_slice method =
            g_calls.back()->ReadSlice(action.create_call().method());
        if (GRPC_SLICE_LENGTH(method) == 0) {
          ok = false;
        }
        grpc_slice host =
            g_calls.back()->ReadSlice(action.create_call().host());
        gpr_timespec deadline = gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_micros(action.create_call().timeout(), GPR_TIMESPAN));

        if (ok) {
          g_calls.back()->SetCall(grpc_channel_create_call(
              g_channel, parent_call == nullptr ? nullptr : parent_call->call(),
              action.create_call().propagation_mask(), cq, method, &host,
              deadline, nullptr));
        } else {
          g_calls.pop_back();
          no_more_actions();
        }
        break;
      }
      // switch the 'current' call
      case api_fuzzer::Action::kChangeActiveCall: {
        g_active_call++;
        ActiveCall();
        break;
      }
      // queue some ops on a call
      case api_fuzzer::Action::kQueueBatch: {
        auto* active_call = ActiveCall();
        if (active_call == nullptr ||
            active_call->type() == CallType::PENDING_SERVER ||
            active_call->call() == nullptr) {
          no_more_actions();
          break;
        }
        const auto& batch = action.queue_batch().operations();
        if (batch.size() > 6) {
          no_more_actions();
          break;
        }
        std::vector<grpc_op> ops;
        bool ok = true;
        uint8_t has_ops = 0;
        std::vector<std::function<void()>> unwinders;
        for (const auto& batch_op : batch) {
          auto op = active_call->ReadOp(batch_op, &ok, &has_ops, &unwinders);
          if (!op.has_value()) continue;
          ops.push_back(*op);
        }

        if (g_channel == nullptr) ok = false;
        if (ok) {
          auto* v = active_call->FinishedBatchValidator(has_ops);
          grpc_call_error error = grpc_call_start_batch(
              active_call->call(), ops.data(), ops.size(), v, nullptr);
          if (error != GRPC_CALL_OK) {
            v->Run(false);
          }
        } else {
          no_more_actions();
          for (auto& unwind : unwinders) {
            unwind();
          }
        }
        break;
      }
      // cancel current call
      case api_fuzzer::Action::kCancelCall: {
        auto* active_call = ActiveCall();
        if (active_call != nullptr && active_call->call() != nullptr) {
          grpc_call_cancel(active_call->call(), nullptr);
        } else {
          no_more_actions();
        }
        break;
      }
      // get a calls peer
      case api_fuzzer::Action::kGetPeer: {
        auto* active_call = ActiveCall();
        if (active_call != nullptr && active_call->call() != nullptr) {
          free_non_null(grpc_call_get_peer(active_call->call()));
        } else {
          no_more_actions();
        }
        break;
      }
      // get a channels target
      case api_fuzzer::Action::kGetTarget: {
        if (g_channel != nullptr) {
          free_non_null(grpc_channel_get_target(g_channel));
        } else {
          no_more_actions();
        }
        break;
      }
      // send a ping on a channel
      case api_fuzzer::Action::kPing: {
        if (g_channel != nullptr) {
          pending_pings++;
          grpc_channel_ping(g_channel, cq, Decrement(&pending_pings), nullptr);
        } else {
          no_more_actions();
        }
        break;
      }
      // enable a tracer
      case api_fuzzer::Action::kEnableTracer: {
        grpc_tracer_set_enabled(action.enable_tracer().c_str(), 1);
        break;
      }
      // disable a tracer
      case api_fuzzer::Action::kDisableTracer: {
        grpc_tracer_set_enabled(action.disable_tracer().c_str(), 0);
        break;
      }
      // request a server call
      case api_fuzzer::Action::kRequestCall: {
        if (g_server == nullptr) {
          no_more_actions();
          break;
        }
        g_calls.emplace_back(new Call(CallType::PENDING_SERVER));
        g_calls.back()->RequestCall(g_server, cq);
        break;
      }
      // destroy a call
      case api_fuzzer::Action::kDestroyCall: {
        auto* active_call = ActiveCall();
        if (active_call != nullptr &&
            active_call->type() != CallType::PENDING_SERVER &&
            active_call->call() != nullptr) {
          g_calls[g_active_call]->Shutdown();
        } else {
          no_more_actions();
        }
        break;
      }
      // resize the buffer pool
      case api_fuzzer::Action::kResizeResourceQuota: {
        grpc_resource_quota_resize(g_resource_quota,
                                   action.resize_resource_quota());
        break;
      }
    }
  }

  GPR_ASSERT(g_channel == nullptr);
  GPR_ASSERT(g_server == nullptr);
  GPR_ASSERT(ActiveCall() == nullptr);
  GPR_ASSERT(g_calls.empty());

  grpc_completion_queue_shutdown(cq);
  GPR_ASSERT(poll_cq());
  grpc_completion_queue_destroy(cq);

  grpc_resource_quota_unref(g_resource_quota);
  grpc_shutdown_blocking();
  FuzzingEventEngine::UnsetGlobalNowImplEngine(engine.get());
}
