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
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/end2end/fuzzers/fuzzing_common.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/fuzz_config_vars.h"
#include "test/core/util/fuzzing_channel_args.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

////////////////////////////////////////////////////////////////////////////////
// logging

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  char* addr;
  grpc_closure* on_done;
  std::unique_ptr<grpc_core::EndpointAddressesList>* addresses;
} addr_req;

static void finish_resolve(addr_req r) {
  if (0 == strcmp(r.addr, "server")) {
    *r.addresses = std::make_unique<grpc_core::EndpointAddressesList>();
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
    std::unique_ptr<grpc_core::EndpointAddressesList>* addresses,
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
// globals

class Call;

namespace grpc {
namespace testing {

class ApiFuzzer : public BasicFuzzer {
 public:
  ApiFuzzer();
  ~ApiFuzzer() override;
  Call* ActiveCall();
  bool Continue();
  void TryShutdown();
  void Tick();
  grpc_server* Server() { return server_; }

 private:
  Result PollCq() override;
  Result CreateChannel(
      const api_fuzzer::CreateChannel& create_channel) override;

  Result CloseChannel() override;
  Result CreateServer(const api_fuzzer::CreateServer& create_server) override;
  Result ShutdownServer() override;
  Result CancelAllCallsIfShutdown() override;
  Result DestroyServerIfReady() override;
  Result CheckConnectivity(bool try_to_connect) override;
  Result WatchConnectivity(uint32_t duration_us) override;
  Result CreateCall(const api_fuzzer::CreateCall& create_call) override;
  Result QueueBatchForActiveCall(const api_fuzzer::Batch& queue_batch) override;
  Result ChangeActiveCall() override;
  Result CancelActiveCall() override;
  Result SendPingOnChannel() override;
  Result ServerRequestCall() override;
  Result DestroyActiveCall() override;
  Result ValidatePeerForActiveCall() override;
  Result ValidateChannelTarget() override;
  Result ResizeResourceQuota(uint32_t resize_resource_quota) override;

  std::shared_ptr<FuzzingEventEngine> engine_;
  grpc_completion_queue* cq_ = nullptr;
  grpc_server* server_ = nullptr;
  grpc_channel* channel_ = nullptr;
  grpc_core::RefCountedPtr<grpc_core::ResourceQuota> resource_quota_;
  std::atomic<bool> channel_force_delete_{false};
  std::vector<std::shared_ptr<Call>> calls_;
  size_t active_call_ = 0;
  bool server_shutdown_ = false;
  int pending_server_shutdowns_ = 0;
  int pending_channel_watches_ = 0;
  int pending_pings_ = 0;
};

}  // namespace testing
}  // namespace grpc

namespace {
grpc::testing::ApiFuzzer* g_api_fuzzer = nullptr;
}

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
        // we get here, then under certain conditions, enqueuing this op will
        // overwrite the internal call->receiving_buffer maintained by grpc
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

namespace grpc_event_engine {
namespace experimental {
extern bool g_event_engine_supports_fd;
}
}  // namespace grpc_event_engine

namespace grpc {
namespace testing {

namespace {
int force_experiments = []() {
  grpc_event_engine::experimental::g_event_engine_supports_fd = false;
  grpc_core::ForceEnableExperiment("event_engine_client", true);
  grpc_core::ForceEnableExperiment("event_engine_listener", true);
  return 1;
}();
}

ApiFuzzer::ApiFuzzer()
    : engine_(std::dynamic_pointer_cast<FuzzingEventEngine>(
          GetDefaultEventEngine())),
      resource_quota_(grpc_core::MakeResourceQuota("api_fuzzer")) {
  grpc_init();
  grpc_timer_manager_set_threading(false);
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
  }
  grpc_core::ResetDNSResolver(
      std::make_unique<FuzzerDNSResolver>(engine_.get()));
  grpc_dns_lookup_hostname_ares = my_dns_lookup_ares;
  grpc_cancel_ares_request = my_cancel_ares_request;

  GPR_ASSERT(channel_ == nullptr);
  GPR_ASSERT(server_ == nullptr);

  cq_ = grpc_completion_queue_create_for_next(nullptr);
}

Call* ApiFuzzer::ActiveCall() {
  while (!calls_.empty()) {
    if (active_call_ >= calls_.size()) {
      active_call_ = 0;
    }
    if (calls_[active_call_] != nullptr && !calls_[active_call_]->done()) {
      return calls_[active_call_].get();
    }
    calls_.erase(calls_.begin() + active_call_);
  }
  return nullptr;
}

bool ApiFuzzer::Continue() {
  return channel_ != nullptr || server_ != nullptr ||
         pending_channel_watches_ > 0 || pending_pings_ > 0 ||
         ActiveCall() != nullptr;
}

void ApiFuzzer::TryShutdown() {
  engine_->FuzzingDone();
  if (channel_ != nullptr) {
    grpc_channel_destroy(channel_);
    channel_ = nullptr;
  }
  if (server_ != nullptr) {
    if (!server_shutdown_) {
      grpc_server_shutdown_and_notify(
          server_, cq_, AssertSuccessAndDecrement(&pending_server_shutdowns_));
      server_shutdown_ = true;
      pending_server_shutdowns_++;
    } else if (pending_server_shutdowns_ == 0) {
      grpc_server_destroy(server_);
      server_ = nullptr;
    }
  }
  for (auto& call : calls_) {
    if (call == nullptr) continue;
    if (call->type() == CallType::PENDING_SERVER) continue;
    call->Shutdown();
  }

  grpc_timer_manager_tick();
  GPR_ASSERT(PollCq() == Result::kPending);
}

ApiFuzzer::~ApiFuzzer() {
  GPR_ASSERT(channel_ == nullptr);
  GPR_ASSERT(server_ == nullptr);
  GPR_ASSERT(ActiveCall() == nullptr);
  GPR_ASSERT(calls_.empty());

  engine_->TickUntilIdle();

  grpc_completion_queue_shutdown(cq_);
  GPR_ASSERT(PollCq() == Result::kComplete);
  grpc_completion_queue_destroy(cq_);

  grpc_shutdown_blocking();
  engine_->UnsetGlobalHooks();
}

void ApiFuzzer::Tick() {
  engine_->Tick();
  grpc_timer_manager_tick();
  if (channel_force_delete_.exchange(false) && channel_) {
    grpc_channel_destroy(channel_);
    channel_ = nullptr;
  }
}

ApiFuzzer::Result ApiFuzzer::PollCq() {
  grpc_event ev = grpc_completion_queue_next(
      cq_, gpr_inf_past(GPR_CLOCK_REALTIME), nullptr);
  switch (ev.type) {
    case GRPC_OP_COMPLETE: {
      static_cast<Validator*>(ev.tag)->Run(ev.success);
      break;
    }
    case GRPC_QUEUE_TIMEOUT:
      break;
    case GRPC_QUEUE_SHUTDOWN:
      return Result::kComplete;
  }
  return Result::kPending;
}
ApiFuzzer::Result ApiFuzzer::CreateChannel(
    const api_fuzzer::CreateChannel& create_channel) {
  if (channel_ == nullptr) return Result::kComplete;
  // ExecCtx is needed for ChannelArgs destruction.
  grpc_core::ExecCtx exec_ctx;
  grpc_core::testing::FuzzingEnvironment fuzzing_env;
  fuzzing_env.resource_quota = resource_quota_;
  grpc_core::ChannelArgs args =
      grpc_core::testing::CreateChannelArgsFromFuzzingConfiguration(
          create_channel.channel_args(), fuzzing_env);
  grpc_channel_credentials* creds =
      create_channel.has_channel_creds()
          ? ReadChannelCreds(create_channel.channel_creds())
          : grpc_insecure_credentials_create();
  channel_ = grpc_channel_create(create_channel.target().c_str(), creds,
                                 args.ToC().get());
  grpc_channel_credentials_release(creds);
  GPR_ASSERT(channel_ != nullptr);
  channel_force_delete_ = false;
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CloseChannel() {
  if (channel_ != nullptr) {
    grpc_channel_destroy(channel_);
    channel_ = nullptr;
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CreateServer(
    const api_fuzzer::CreateServer& create_server) {
  if (server_ == nullptr) {
    // ExecCtx is needed for ChannelArgs destruction.
    grpc_core::ExecCtx exec_ctx;
    grpc_core::testing::FuzzingEnvironment fuzzing_env;
    fuzzing_env.resource_quota = resource_quota_;
    grpc_core::ChannelArgs args =
        grpc_core::testing::CreateChannelArgsFromFuzzingConfiguration(
            create_server.channel_args(), fuzzing_env);
    server_ = grpc_server_create(args.ToC().get(), nullptr);
    GPR_ASSERT(server_ != nullptr);
    grpc_server_register_completion_queue(server_, cq_, nullptr);
    grpc_server_start(server_);
    server_shutdown_ = false;
    GPR_ASSERT(pending_server_shutdowns_ == 0);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ShutdownServer() {
  if (server_ != nullptr) {
    grpc_server_shutdown_and_notify(
        server_, cq_, AssertSuccessAndDecrement(&pending_server_shutdowns_));
    pending_server_shutdowns_++;
    server_shutdown_ = true;
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CancelAllCallsIfShutdown() {
  if (server_ != nullptr && server_shutdown_) {
    grpc_server_cancel_all_calls(server_);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::DestroyServerIfReady() {
  if (server_ != nullptr && server_shutdown_ &&
      pending_server_shutdowns_ == 0) {
    grpc_server_destroy(server_);
    server_ = nullptr;
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CheckConnectivity(bool try_to_connect) {
  if (channel_ != nullptr) {
    grpc_channel_check_connectivity_state(channel_, try_to_connect);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::WatchConnectivity(uint32_t duration_us) {
  if (channel_ != nullptr) {
    grpc_connectivity_state st =
        grpc_channel_check_connectivity_state(channel_, 0);
    if (st != GRPC_CHANNEL_SHUTDOWN) {
      gpr_timespec deadline =
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_micros(duration_us, GPR_TIMESPAN));
      grpc_channel_watch_connectivity_state(
          channel_, st, deadline, cq_,
          ValidateConnectivityWatch(deadline, &pending_channel_watches_));
      pending_channel_watches_++;
    }
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CreateCall(
    const api_fuzzer::CreateCall& create_call) {
  bool ok = true;
  if (channel_ == nullptr) ok = false;
  // If the active call is a server call, then use it as the parent call
  // to exercise the propagation logic.
  Call* parent_call = ActiveCall();
  if (parent_call != nullptr && parent_call->type() != CallType::SERVER) {
    parent_call = nullptr;
  }
  calls_.emplace_back(new Call(CallType::CLIENT));
  grpc_slice method = calls_.back()->ReadSlice(create_call.method());
  if (GRPC_SLICE_LENGTH(method) == 0) {
    ok = false;
  }
  grpc_slice host = calls_.back()->ReadSlice(create_call.host());
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_micros(create_call.timeout(), GPR_TIMESPAN));

  if (ok) {
    calls_.back()->SetCall(grpc_channel_create_call(
        channel_, parent_call == nullptr ? nullptr : parent_call->call(),
        create_call.propagation_mask(), cq_, method, &host, deadline, nullptr));
  } else {
    calls_.pop_back();
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ChangeActiveCall() {
  active_call_++;
  ActiveCall();
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::QueueBatchForActiveCall(
    const api_fuzzer::Batch& queue_batch) {
  auto* active_call = ActiveCall();
  if (active_call == nullptr ||
      active_call->type() == CallType::PENDING_SERVER ||
      active_call->call() == nullptr) {
    return Result::kFailed;
  }
  const auto& batch = queue_batch.operations();
  if (batch.size() > 6) {
    return Result::kFailed;
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

  if (channel_ == nullptr) ok = false;
  if (ok) {
    auto* v = active_call->FinishedBatchValidator(has_ops);
    grpc_call_error error = grpc_call_start_batch(
        active_call->call(), ops.data(), ops.size(), v, nullptr);
    if (error != GRPC_CALL_OK) {
      v->Run(false);
    }
  } else {
    for (auto& unwind : unwinders) {
      unwind();
    }
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CancelActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr && active_call->call() != nullptr) {
    grpc_call_cancel(active_call->call(), nullptr);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::SendPingOnChannel() {
  if (channel_ != nullptr) {
    pending_pings_++;
    grpc_channel_ping(channel_, cq_, Decrement(&pending_pings_), nullptr);
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ServerRequestCall() {
  if (server_ == nullptr) {
    return Result::kFailed;
  }
  calls_.emplace_back(new Call(CallType::PENDING_SERVER));
  calls_.back()->RequestCall(server_, cq_);
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::DestroyActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr &&
      active_call->type() != CallType::PENDING_SERVER &&
      active_call->call() != nullptr) {
    calls_[active_call_]->Shutdown();
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ValidatePeerForActiveCall() {
  auto* active_call = ActiveCall();
  if (active_call != nullptr && active_call->call() != nullptr) {
    free_non_null(grpc_call_get_peer(active_call->call()));
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ValidateChannelTarget() {
  if (channel_ != nullptr) {
    free_non_null(grpc_channel_get_target(channel_));
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::ResizeResourceQuota(
    uint32_t resize_resource_quota) {
  resource_quota_->memory_quota()->SetSize(resize_resource_quota);
  return Result::kComplete;
}

}  // namespace testing
}  // namespace grpc

DEFINE_PROTO_FUZZER(const api_fuzzer::Msg& msg) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  grpc_event_engine::experimental::SetEventEngineFactory(
      [actions = msg.event_engine_actions()]() {
        return std::make_unique<FuzzingEventEngine>(
            FuzzingEventEngine::Options(), actions);
      });

  g_api_fuzzer = new grpc::testing::ApiFuzzer();
  int action_index = 0;
  auto no_more_actions = [&]() { action_index = msg.actions_size(); };

  while (action_index < msg.actions_size() || g_api_fuzzer->Continue()) {
    g_api_fuzzer->Tick();

    if (action_index == msg.actions_size()) {
      g_api_fuzzer->TryShutdown();
      continue;
    }

    auto result = g_api_fuzzer->ExecuteAction(msg.actions(action_index++));
    if (result == grpc::testing::ApiFuzzer::Result::kFailed) {
      no_more_actions();
    }
  }
  delete g_api_fuzzer;
}
