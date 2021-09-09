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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/server/binder_server.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc {
namespace experimental {
namespace binder {

void* GetEndpointBinder(const std::string& service) {
  return grpc_get_endpoint_binder(service);
}

void AddEndpointBinder(const std::string& service, void* endpoint_binder) {
  grpc_add_endpoint_binder(service, endpoint_binder);
}

void RemoveEndpointBinder(const std::string& service) {
  grpc_remove_endpoint_binder(service);
}

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

static absl::flat_hash_map<std::string, void*>* g_endpoint_binder_pool =
    nullptr;

namespace {

grpc_core::Mutex* GetBinderPoolMutex() {
  static grpc_core::Mutex* mu = new grpc_core::Mutex();
  return mu;
}

}  // namespace

void grpc_add_endpoint_binder(const std::string& service,
                              void* endpoint_binder) {
  grpc_core::MutexLock lock(GetBinderPoolMutex());
  if (g_endpoint_binder_pool == nullptr) {
    g_endpoint_binder_pool = new absl::flat_hash_map<std::string, void*>();
  }
  (*g_endpoint_binder_pool)[service] = endpoint_binder;
}

void grpc_remove_endpoint_binder(const std::string& service) {
  grpc_core::MutexLock lock(GetBinderPoolMutex());
  if (g_endpoint_binder_pool == nullptr) {
    return;
  }
  g_endpoint_binder_pool->erase(service);
}

void* grpc_get_endpoint_binder(const std::string& service) {
  grpc_core::MutexLock lock(GetBinderPoolMutex());
  if (g_endpoint_binder_pool == nullptr) {
    return nullptr;
  }
  auto iter = g_endpoint_binder_pool->find(service);
  return iter == g_endpoint_binder_pool->end() ? nullptr : iter->second;
}

namespace grpc_core {

class BinderServerListener : public Server::ListenerInterface {
 public:
  BinderServerListener(Server* server, std::string addr,
                       BinderTxReceiverFactory factory)
      : server_(server), addr_(std::move(addr)), factory_(std::move(factory)) {}

  void Start(Server* /*server*/,
             const std::vector<grpc_pollset*>* /*pollsets*/) override {
    tx_receiver_ = factory_([this](transaction_code_t code,
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
  BinderTxReceiverFactory factory_;
  void* endpoint_binder_ = nullptr;
  std::unique_ptr<grpc_binder::TransactionReceiver> tx_receiver_;
};

bool AddBinderPort(const std::string& addr, grpc_server* server,
                   BinderTxReceiverFactory factory) {
  const std::string kBinderUriScheme = "binder:";
  if (addr.compare(0, kBinderUriScheme.size(), kBinderUriScheme) != 0) {
    return false;
  }
  size_t pos = kBinderUriScheme.size();
  while (pos < addr.size() && addr[pos] == '/') pos++;
  grpc_core::Server* core_server = server->core_server.get();
  core_server->AddListener(
      grpc_core::OrphanablePtr<grpc_core::Server::ListenerInterface>(
          new grpc_core::BinderServerListener(core_server, addr.substr(pos),
                                              std::move(factory))));
  return true;
}

}  // namespace grpc_core
