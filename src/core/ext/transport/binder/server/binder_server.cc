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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/server/binder_server.h"

#ifndef GRPC_NO_BINDER

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/utils/ndk_binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <jni.h>

extern "C" {

// This will be invoked from
// src/core/ext/transport/binder/java/io/grpc/binder/cpp/GrpcCppServerBuilder.java
JNIEXPORT jobject JNICALL
Java_io_grpc_binder_cpp_GrpcCppServerBuilder_GetEndpointBinderInternal__Ljava_lang_String_2(
    JNIEnv* jni_env, jobject, jstring conn_id_jstring) {
  grpc_binder::ndk_util::AIBinder* ai_binder = nullptr;

  {
    // This block is the scope of conn_id c-string
    jboolean isCopy;
    const char* conn_id = jni_env->GetStringUTFChars(conn_id_jstring, &isCopy);
    ai_binder = static_cast<grpc_binder::ndk_util::AIBinder*>(
        grpc_get_endpoint_binder(std::string(conn_id)));
    if (ai_binder == nullptr) {
      gpr_log(GPR_ERROR, "Cannot find endpoint binder with connection id = %s",
              conn_id);
    }
    if (isCopy == JNI_TRUE) {
      jni_env->ReleaseStringUTFChars(conn_id_jstring, conn_id);
    }
  }

  if (ai_binder == nullptr) {
    return nullptr;
  }

  return grpc_binder::ndk_util::AIBinder_toJavaBinder(jni_env, ai_binder);
}
}

#endif

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
  BinderServerListener(
      Server* server, std::string addr, BinderTxReceiverFactory factory,
      std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
          security_policy)
      : server_(server),
        addr_(std::move(addr)),
        factory_(std::move(factory)),
        security_policy_(security_policy) {}

  void Start(Server* /*server*/,
             const std::vector<grpc_pollset*>* /*pollsets*/) override {
    tx_receiver_ = factory_(
        [this](transaction_code_t code, grpc_binder::ReadableParcel* parcel,
               int uid) { return OnSetupTransport(code, parcel, uid); });
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
      ExecCtx::Run(DEBUG_LOCATION, on_destroy_done_, absl::OkStatus());
      ExecCtx::Get()->Flush();
    }
    grpc_remove_endpoint_binder(addr_);
  }

 private:
  absl::Status OnSetupTransport(transaction_code_t code,
                                grpc_binder::ReadableParcel* parcel, int uid) {
    ExecCtx exec_ctx;
    if (static_cast<grpc_binder::BinderTransportTxCode>(code) !=
        grpc_binder::BinderTransportTxCode::SETUP_TRANSPORT) {
      return absl::InvalidArgumentError("Not a SETUP_TRANSPORT request");
    }

    gpr_log(GPR_INFO, "BinderServerListener calling uid = %d", uid);
    if (!security_policy_->IsAuthorized(uid)) {
      // TODO(mingcl): For now we just ignore this unauthorized
      // SETUP_TRANSPORT transaction and ghost the client. Check if we should
      // send back a SHUTDOWN_TRANSPORT in this case.
      return absl::PermissionDeniedError(
          "UID " + std::to_string(uid) +
          " is not allowed to connect to this "
          "server according to security policy.");
    }

    int version;
    absl::Status status = parcel->ReadInt32(&version);
    if (!status.ok()) {
      return status;
    }
    gpr_log(GPR_INFO, "BinderTransport client protocol version = %d", version);
    // TODO(mingcl): Make sure we only give client a version that is not newer
    // than the version they specify. For now, we always tell client that we
    // only support version=1.
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
    grpc_transport* server_transport = grpc_create_binder_transport_server(
        std::move(client_binder), security_policy_);
    GPR_ASSERT(server_transport);
    grpc_error_handle error = server_->SetupTransport(
        server_transport, nullptr, server_->channel_args(), nullptr);
    return grpc_error_to_absl_status(error);
  }

  Server* server_;
  grpc_closure* on_destroy_done_ = nullptr;
  std::string addr_;
  BinderTxReceiverFactory factory_;
  std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy_;
  void* endpoint_binder_ = nullptr;
  std::unique_ptr<grpc_binder::TransactionReceiver> tx_receiver_;
};

bool AddBinderPort(const std::string& addr, grpc_server* server,
                   BinderTxReceiverFactory factory,
                   std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
                       security_policy) {
  // TODO(mingcl): Check if the addr is valid here after binder address resolver
  // related code are merged.
  const std::string kBinderUriScheme = "binder:";
  if (addr.compare(0, kBinderUriScheme.size(), kBinderUriScheme) != 0) {
    return false;
  }
  std::string conn_id = addr.substr(kBinderUriScheme.size());
  Server* core_server = Server::FromC(server);
  core_server->AddListener(
      OrphanablePtr<Server::ListenerInterface>(new BinderServerListener(
          core_server, conn_id, std::move(factory), security_policy)));
  return true;
}

}  // namespace grpc_core
#endif
