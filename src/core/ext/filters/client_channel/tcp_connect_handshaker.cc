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
#include "src/core/ext/filters/client_channel/tcp_connect_handshaker.h"

#include <string.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/tcp_client.h"

namespace grpc_core {

namespace {

class TCPConnectHandshaker : public Handshaker {
 public:
  TCPConnectHandshaker(grpc_pollset_set* interested_parties);
  void Shutdown(grpc_error_handle why) override;
  void DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override;
  const char* name() const override { return "tcp_connect"; }

 private:
  ~TCPConnectHandshaker() override;
  void CleanupArgsForFailureLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void Connected(void* arg, grpc_error_handle error);

  Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_slice_buffer* read_buffer_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;
  grpc_closure* on_handshake_done_ = nullptr;
  grpc_pollset_set* interested_parties_ = nullptr;
  HandshakerArgs* args_ = nullptr;
  grpc_closure connected_;
};

TCPConnectHandshaker::TCPConnectHandshaker(grpc_pollset_set* interested_parties)
    : interested_parties_(interested_parties) {
  GRPC_CLOSURE_INIT(&connected_, Connected, this, grpc_schedule_on_exec_ctx);
}

void TCPConnectHandshaker::Shutdown(grpc_error_handle why) {
  // TODO(anramach): After migration to EventEngine, cancel the in-progress
  // TCP connection attempt.
  {
    MutexLock lock(&mu_);
    shutdown_ = true;
    // If the TCP connect had ever succeded, shutdown.
    if (args_->endpoint != nullptr) {
      grpc_endpoint_shutdown(args_->endpoint, GRPC_ERROR_REF(why));
    }
    CleanupArgsForFailureLocked();
  }
  GRPC_ERROR_UNREF(why);
}

void TCPConnectHandshaker::DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                                       grpc_closure* on_handshake_done,
                                       HandshakerArgs* args) {
  on_handshake_done_ = on_handshake_done;
  args_ = args;
  // In some implementations, the closure can be flushed before
  // grpc_tcp_client_connect() returns, and since the closure requires access
  // to mu_, this can result in a deadlock (see
  // https://github.com/grpc/grpc/issues/16427 for details).
  // grpc_tcp_client_connect() will fill endpoint_ with proper contents, and we
  // make sure that we still exist at that point by taking a ref.
  Ref().release();  // Ref held by callback.
  grpc_tcp_client_connect(&connected_, &args->endpoint, interested_parties_,
                          args->args, &args->connect_args->address,
                          args->connect_args->deadline);
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
      if (!self->shutdown_) {
        if (self->args_->endpoint != nullptr) {
          grpc_endpoint_shutdown(self->args_->endpoint, GRPC_ERROR_REF(error));
        }
        self->CleanupArgsForFailureLocked();
        self->shutdown_ = true;
      }
      ExecCtx::Run(DEBUG_LOCATION, self->on_handshake_done_, error);
      return;
    }
    GPR_ASSERT(self->args_->endpoint != nullptr);
    if (self->args_->connect_args->bind_endpoint_to_pollset) {
      grpc_endpoint_add_to_pollset_set(self->args_->endpoint,
                                       self->interested_parties_);
    }
    ExecCtx::Run(DEBUG_LOCATION, self->on_handshake_done_, GRPC_ERROR_NONE);
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
}

void TCPConnectHandshaker::CleanupArgsForFailureLocked() {
  endpoint_to_destroy_ = args_->endpoint;
  args_->endpoint = nullptr;
  read_buffer_to_destroy_ = args_->read_buffer;
  args_->read_buffer = nullptr;
  grpc_channel_args_destroy(args_->args);
  args_->args = nullptr;
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
