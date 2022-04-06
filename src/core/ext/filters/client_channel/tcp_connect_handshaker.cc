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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/tcp_connect_handshaker.h"

#include <string.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/tcp_client.h"

namespace grpc_core {

namespace {

class TCPConnectHandshaker : public Handshaker {
 public:
  explicit TCPConnectHandshaker(grpc_pollset_set* pollset_set);
  void Shutdown(grpc_error_handle why) override;
  void DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override;
  const char* name() const override { return "tcp_connect"; }

 private:
  ~TCPConnectHandshaker() override;
  void CleanupArgsForFailureLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void FinishLocked(grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void Connected(void* arg, grpc_error_handle error);

  Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_slice_buffer* read_buffer_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_closure* on_handshake_done_ ABSL_GUARDED_BY(mu_) = nullptr;
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
  grpc_polling_entity_add_to_pollset_set(&pollent_, interested_parties_);
  GRPC_CLOSURE_INIT(&connected_, Connected, this, grpc_schedule_on_exec_ctx);
}

void TCPConnectHandshaker::Shutdown(grpc_error_handle why) {
  // TODO(anramach): After migration to EventEngine, cancel the in-progress
  // TCP connection attempt.
  {
    MutexLock lock(&mu_);
    if (!shutdown_) {
      shutdown_ = true;
      // If we are shutting down while connecting, respond back with
      // handshake done.
      // The callback from grpc_tcp_client_connect will perform
      // the necessary clean up.
      if (on_handshake_done_ != nullptr) {
        CleanupArgsForFailureLocked();
        FinishLocked(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("tcp handshaker shutdown"));
      }
    }
  }
  GRPC_ERROR_UNREF(why);
}

void TCPConnectHandshaker::DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                                       grpc_closure* on_handshake_done,
                                       HandshakerArgs* args) {
  {
    MutexLock lock(&mu_);
    on_handshake_done_ = on_handshake_done;
  }
  GPR_ASSERT(args->endpoint == nullptr);
  args_ = args;
  char* address = grpc_channel_args_find_string(
      args->args, GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS);
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address);
  if (!uri.ok() || !grpc_parse_uri(*uri, &addr_)) {
    MutexLock lock(&mu_);
    FinishLocked(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Resolved address in invalid format"));
    return;
  }
  bind_endpoint_to_pollset_ = grpc_channel_args_find_bool(
      args->args, GRPC_ARG_TCP_HANDSHAKER_BIND_ENDPOINT_TO_POLLSET, false);
  const char* args_to_remove[] = {
      GRPC_ARG_TCP_HANDSHAKER_RESOLVED_ADDRESS,
      GRPC_ARG_TCP_HANDSHAKER_BIND_ENDPOINT_TO_POLLSET};
  // Update args to not contain the args relevant to TCP connect handshaker.
  grpc_channel_args* channel_args = grpc_channel_args_copy_and_remove(
      args->args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove));
  grpc_channel_args_destroy(args->args);
  args->args = channel_args;
  // In some implementations, the closure can be flushed before
  // grpc_tcp_client_connect() returns, and since the closure requires access
  // to mu_, this can result in a deadlock (see
  // https://github.com/grpc/grpc/issues/16427 for details).
  // grpc_tcp_client_connect() will fill endpoint_ with proper contents, and we
  // make sure that we still exist at that point by taking a ref.
  Ref().release();  // Ref held by callback.
  // As we fake the TCP client connection failure when shutdown is called
  // we don't want to pass args->endpoint directly.
  // Instead pass endpoint_ and swap this endpoint to
  // args endpoint on success.
  grpc_tcp_client_connect(&connected_, &endpoint_to_destroy_,
                          interested_parties_, args->args, &addr_,
                          args->deadline);
}

void TCPConnectHandshaker::Connected(void* arg, grpc_error_handle error) {
  RefCountedPtr<TCPConnectHandshaker> self(
      static_cast<TCPConnectHandshaker*>(arg));
  {
    MutexLock lock(&self->mu_);
    if (error != GRPC_ERROR_NONE || self->shutdown_) {
      if (error == GRPC_ERROR_NONE) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("tcp handshaker shutdown");
      } else {
        error = GRPC_ERROR_REF(error);
      }
      if (self->endpoint_to_destroy_ != nullptr) {
        grpc_endpoint_shutdown(self->endpoint_to_destroy_,
                               GRPC_ERROR_REF(error));
      }
      if (!self->shutdown_) {
        self->CleanupArgsForFailureLocked();
        self->shutdown_ = true;
        self->FinishLocked(error);
      } else {
        // The on_handshake_done_ is already as part of shutdown when connecting
        // So nothing to be done here other than unrefing the error.
        GRPC_ERROR_UNREF(error);
      }
      return;
    }
    GPR_ASSERT(self->endpoint_to_destroy_ != nullptr);
    self->args_->endpoint = self->endpoint_to_destroy_;
    self->endpoint_to_destroy_ = nullptr;
    if (self->bind_endpoint_to_pollset_) {
      grpc_endpoint_add_to_pollset_set(self->args_->endpoint,
                                       self->interested_parties_);
    }
    self->FinishLocked(GRPC_ERROR_NONE);
  }
}

TCPConnectHandshaker::~TCPConnectHandshaker() {
  if (endpoint_to_destroy_ != nullptr) {
    grpc_endpoint_destroy(endpoint_to_destroy_);
  }
  if (read_buffer_to_destroy_ != nullptr) {
    grpc_slice_buffer_destroy_internal(read_buffer_to_destroy_);
    gpr_free(read_buffer_to_destroy_);
  }
  grpc_pollset_set_destroy(interested_parties_);
}

void TCPConnectHandshaker::CleanupArgsForFailureLocked() {
  read_buffer_to_destroy_ = args_->read_buffer;
  args_->read_buffer = nullptr;
  grpc_channel_args_destroy(args_->args);
  args_->args = nullptr;
}

void TCPConnectHandshaker::FinishLocked(grpc_error_handle error) {
  grpc_polling_entity_del_from_pollset_set(&pollent_, interested_parties_);
  ExecCtx::Run(DEBUG_LOCATION, on_handshake_done_, error);
  on_handshake_done_ = nullptr;
}

//
// TCPConnectHandshakerFactory
//

class TCPConnectHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const grpc_channel_args* /*args*/,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(
        MakeRefCounted<TCPConnectHandshaker>(interested_parties));
  }
  ~TCPConnectHandshakerFactory() override = default;
};

}  // namespace

void RegisterTCPConnectHandshaker(CoreConfiguration::Builder* builder) {
  builder->handshaker_registry()->RegisterHandshakerFactory(
      true /* at_start */, HANDSHAKER_CLIENT,
      absl::make_unique<TCPConnectHandshakerFactory>());
}

}  // namespace grpc_core
