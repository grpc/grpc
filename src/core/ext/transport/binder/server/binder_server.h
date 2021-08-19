// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H

#include <grpc/impl/codegen/port_platform.h>

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"

// TODO(waynetu): This is part of the public API and should be moved to the
// include/ folder.
namespace grpc {
namespace experimental {
namespace binder {

void* GetEndpointBinder(const std::string& service);
void AddEndpointBinder(const std::string& service, void* endpoint_binder);
void RemoveEndpointBinder(const std::string& service);

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

extern grpc_core::Mutex* g_endpoint_binder_pool_mu;
extern absl::flat_hash_map<std::string, void*>* g_endpoint_binder_pool;

// TODO(waynetu): Can these two functions be called in grpc_init() and
// grpc_shutdown()?
void grpc_endpoint_binder_pool_init();
void grpc_endpoint_binder_pool_shutdown();

void grpc_add_endpoint_binder(const std::string& service,
                              void* endpoint_binder);
void grpc_remove_endpoint_binder(const std::string& service);
void* grpc_get_endpoint_binder(const std::string& service);

namespace grpc_core {

template <typename T>
class BinderServerListener : public Server::ListenerInterface {
 public:
  BinderServerListener(Server* server, std::string addr)
      : server_(server), addr_(std::move(addr)) {}

  void Start(Server* /*server*/,
             const std::vector<grpc_pollset*>* /*pollsets*/) override {
    tx_receiver_ = absl::make_unique<T>(
        nullptr, [this](transaction_code_t code,
                        const grpc_binder::ReadableParcel* parcel) {
          return OnSetupTransport(code, parcel);
        });
    endpoint_binder_ = tx_receiver_->GetRawBinder();
    grpc_add_endpoint_binder(addr_, endpoint_binder_);
  }

  channelz::ListenSocketNode* channelz_listen_socket_node() const override {
    return nullptr;
  }

  void SetOnDestroyDone(grpc_closure* on_destroy_done) override {
    on_destroy_done_ = on_destroy_done;
  }

  void Orphan() override { delete this; }

  ~BinderServerListener() override {
    ExecCtx::Get()->Flush();
    if (on_destroy_done_) {
      ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, GRPC_ERROR_NONE);
      ExecCtx::Get()->Flush();
    }
    grpc_remove_endpoint_binder(addr_);
  }

 private:
  absl::Status OnSetupTransport(transaction_code_t code,
                                const grpc_binder::ReadableParcel* parcel) {
    grpc_core::ExecCtx exec_ctx;
    if (grpc_binder::BinderTransportTxCode(code) !=
        grpc_binder::BinderTransportTxCode::SETUP_TRANSPORT) {
      return absl::InvalidArgumentError("Not a SETUP_TRANSPORT request");
    }
    int version;
    absl::Status status = parcel->ReadInt32(&version);
    if (!status.ok()) {
      return status;
    }
    gpr_log(GPR_INFO, "version = %d", version);
    // TODO(waynetu): Check supported version.
    std::unique_ptr<grpc_binder::Binder> client_binder{};
    status = parcel->ReadBinder(&client_binder);
    if (!status.ok()) {
      return status;
    }
    if (!client_binder) {
      return absl::InvalidArgumentError("NULL binder read from the parcel");
    }
    client_binder->Initialize();
    // Finish the second half of SETUP_TRANSPORT in
    // grpc_create_binder_transport_server().
    grpc_transport* server_transport =
        grpc_create_binder_transport_server(std::move(client_binder));
    GPR_ASSERT(server_transport);
    grpc_channel_args* args = grpc_channel_args_copy(server_->channel_args());
    grpc_error_handle error = server_->SetupTransport(server_transport, nullptr,
                                                      args, nullptr, nullptr);
    grpc_channel_args_destroy(args);
    return grpc_error_to_absl_status(error);
  }

  Server* server_;
  grpc_closure* on_destroy_done_ = nullptr;
  std::string addr_;
  void* endpoint_binder_ = nullptr;
  std::unique_ptr<grpc_binder::TransactionReceiver> tx_receiver_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H
