//
//
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
//
//

#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"

#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

class TCPConnectHandshaker : public Handshaker {
 public:
  explicit TCPConnectHandshaker(grpc_pollset_set* pollset_set);
  absl::string_view name() const override { return "tcp_connect"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;
  void Shutdown(absl::Status error) override;

 private:
  ~TCPConnectHandshaker() override;
  void FinishLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void Connected(void* arg, grpc_error_handle error);

  Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Endpoint to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_
      ABSL_GUARDED_BY(mu_);
  grpc_pollset_set* interested_parties_ = nullptr;
  grpc_polling_entity pollent_;
  HandshakerArgs* args_ = nullptr;
  bool bind_endpoint_to_pollset_ = false;
  grpc_resolved_address addr_;
  grpc_closure connected_;
};

TCPConnectHandshaker::TCPConnectHandshaker(grpc_pollset_set* pollset_set)
    : interested_parties_(grpc_pollset_set_create()),
      pollent_(grpc_polling_entity_create_from_pollset_set(pollset_set)) {
  // Interested parties might be null for platforms like Apple.
  // Explicitly check before adding/deleting from pollset_set to handle this
  // use case.
  if (interested_parties_ != nullptr) {
    grpc_polling_entity_add_to_pollset_set(&pollent_, interested_parties_);
  }
  GRPC_CLOSURE_INIT(&connected_, Connected, this, grpc_schedule_on_exec_ctx);
}

void TCPConnectHandshaker::Shutdown(absl::Status /*error*/) {
  // TODO(anramach): After migration to EventEngine, cancel the in-progress
  // TCP connection attempt.
  MutexLock lock(&mu_);
  if (!shutdown_) {
    shutdown_ = true;
    // If we are shutting down while connecting, respond back with
    // handshake done.
    // The callback from grpc_tcp_client_connect will perform
    // the necessary clean up.
    if (on_handshake_done_ != nullptr) {
      // TODO(roth): When we remove the legacy grpc_error APIs, propagate the
      // status passed to shutdown as part of the message here.
      FinishLocked(GRPC_ERROR_CREATE("tcp handshaker shutdown"));
    }
  }
}

void TCPConnectHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  {
    MutexLock lock(&mu_);
    on_handshake_done_ = std::move(on_handshake_done);
  }
  CHECK_EQ(args->endpoint.get(), nullptr);
  args_ = args;
  absl::StatusOr<URI> uri = URI::Parse(
      args->args.GetString(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS).value());
  if (!uri.ok() || !grpc_parse_uri(*uri, &addr_)) {
    MutexLock lock(&mu_);
    FinishLocked(GRPC_ERROR_CREATE("Resolved address in invalid format"));
    return;
  }
  bind_endpoint_to_pollset_ =
      args->args.GetBool(GRPC_ARG_TCP_HANDSHAKER_BIND_ENDPOINT_TO_POLLSET)
          .value_or(false);
  // Update args to not contain the args relevant to TCP connect handshaker.
  args->args = args->args.Remove(GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS)
                   .Remove(GRPC_ARG_TCP_HANDSHAKER_BIND_ENDPOINT_TO_POLLSET);
  // In some implementations, the closure can be flushed before
  // grpc_tcp_client_connect() returns, and since the closure requires access
  // to mu_, this can result in a deadlock (see
  // https://github.com/grpc/grpc/issues/16427 for details).
  // grpc_tcp_client_connect() will fill endpoint_ with proper contents, and we
  // make sure that we still exist at that point by taking a ref.
  Ref().release();  // Ref held by callback.
  // As we fake the TCP client connection failure when shutdown is called
  // we don't want to pass args->endpoint directly.
  // Instead pass endpoint_to_destroy_ and swap this endpoint to
  // args endpoint on success.
  grpc_tcp_client_connect(
      &connected_, &endpoint_to_destroy_, interested_parties_,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args->args),
      &addr_, args->deadline);
}

void TCPConnectHandshaker::Connected(void* arg, grpc_error_handle error) {
  RefCountedPtr<TCPConnectHandshaker> self(
      static_cast<TCPConnectHandshaker*>(arg));
  {
    MutexLock lock(&self->mu_);
    if (!error.ok() || self->shutdown_) {
      if (error.ok()) {
        error = GRPC_ERROR_CREATE("tcp handshaker shutdown");
      }
      if (self->endpoint_to_destroy_ != nullptr) {
        grpc_endpoint_destroy(self->endpoint_to_destroy_);
        self->endpoint_to_destroy_ = nullptr;
      }
      if (!self->shutdown_) {
        self->shutdown_ = true;
        self->FinishLocked(std::move(error));
      } else {
        // The on_handshake_done_ callback was already invoked as part of
        // shutdown when connecting, so nothing to be done here.
      }
      return;
    }
    CHECK_NE(self->endpoint_to_destroy_, nullptr);
    self->args_->endpoint.reset(self->endpoint_to_destroy_);
    self->endpoint_to_destroy_ = nullptr;
    if (self->bind_endpoint_to_pollset_) {
      grpc_endpoint_add_to_pollset_set(self->args_->endpoint.get(),
                                       self->interested_parties_);
    }
    self->FinishLocked(absl::OkStatus());
  }
}

TCPConnectHandshaker::~TCPConnectHandshaker() {
  if (endpoint_to_destroy_ != nullptr) {
    grpc_endpoint_destroy(endpoint_to_destroy_);
  }
  grpc_pollset_set_destroy(interested_parties_);
}

void TCPConnectHandshaker::FinishLocked(absl::Status error) {
  if (interested_parties_ != nullptr) {
    grpc_polling_entity_del_from_pollset_set(&pollent_, interested_parties_);
  }
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_), std::move(error));
}

//
// TCPConnectHandshakerFactory
//

class TCPConnectHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const ChannelArgs& /*args*/,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(
        MakeRefCounted<TCPConnectHandshaker>(interested_parties));
  }
  HandshakerPriority Priority() override {
    return HandshakerPriority::kTCPConnectHandshakers;
  }
  ~TCPConnectHandshakerFactory() override = default;
};

}  // namespace

void RegisterTCPConnectHandshaker(CoreConfiguration::Builder* builder) {
  builder->handshaker_registry()->RegisterHandshakerFactory(
      HANDSHAKER_CLIENT, std::make_unique<TCPConnectHandshakerFactory>());
}

}  // namespace grpc_core
