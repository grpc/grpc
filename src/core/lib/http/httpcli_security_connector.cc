/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/http/httpcli.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/string_view.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl_transport_security.h"

class grpc_httpcli_ssl_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_httpcli_ssl_channel_security_connector(char* secure_peer_name)
      : grpc_channel_security_connector(
            /*url_scheme=*/nullptr,
            /*channel_creds=*/nullptr,
            /*request_metadata_creds=*/nullptr),
        secure_peer_name_(secure_peer_name) {}

  ~grpc_httpcli_ssl_channel_security_connector() override {
    if (handshaker_factory_ != nullptr) {
      tsi_ssl_client_handshaker_factory_unref(handshaker_factory_);
    }
    if (secure_peer_name_ != nullptr) {
      gpr_free(secure_peer_name_);
    }
  }

  tsi_result InitHandshakerFactory(const char* pem_root_certs,
                                   const tsi_ssl_root_certs_store* root_store) {
    tsi_ssl_client_handshaker_options options;
    options.pem_root_certs = pem_root_certs;
    options.root_store = root_store;
    return tsi_create_ssl_client_handshaker_factory_with_options(
        &options, &handshaker_factory_);
  }

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    tsi_handshaker* handshaker = nullptr;
    if (handshaker_factory_ != nullptr) {
      tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
          handshaker_factory_, secure_peer_name_, &handshaker);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
                tsi_result_to_string(result));
      }
    }
    handshake_mgr->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this, args));
  }

  tsi_ssl_client_handshaker_factory* handshaker_factory() const {
    return handshaker_factory_;
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* /*auth_context*/,
                  grpc_closure* on_peer_checked) override {
    grpc_error* error = GRPC_ERROR_NONE;

    /* Check the peer name. */
    if (secure_peer_name_ != nullptr &&
        !tsi_ssl_peer_matches_name(&peer, secure_peer_name_)) {
      char* msg;
      gpr_asprintf(&msg, "Peer name %s is not in peer certificate",
                   secure_peer_name_);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_httpcli_ssl_channel_security_connector*>(
            other_sc);
    return strcmp(secure_peer_name_, other->secure_peer_name_);
  }

  bool check_call_host(grpc_core::StringView /*host*/,
                       grpc_auth_context* /*auth_context*/,
                       grpc_closure* /*on_call_host_checked*/,
                       grpc_error** error) override {
    *error = GRPC_ERROR_NONE;
    return true;
  }

  void cancel_check_call_host(grpc_closure* /*on_call_host_checked*/,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

  const char* secure_peer_name() const { return secure_peer_name_; }

 private:
  tsi_ssl_client_handshaker_factory* handshaker_factory_ = nullptr;
  char* secure_peer_name_;
};

static grpc_core::RefCountedPtr<grpc_channel_security_connector>
httpcli_ssl_channel_security_connector_create(
    const char* pem_root_certs, const tsi_ssl_root_certs_store* root_store,
    const char* secure_peer_name, grpc_channel_args* /*channel_args*/) {
  if (secure_peer_name != nullptr && pem_root_certs == nullptr) {
    gpr_log(GPR_ERROR,
            "Cannot assert a secure peer name without a trust root.");
    return nullptr;
  }
  grpc_core::RefCountedPtr<grpc_httpcli_ssl_channel_security_connector> c =
      grpc_core::MakeRefCounted<grpc_httpcli_ssl_channel_security_connector>(
          secure_peer_name == nullptr ? nullptr : gpr_strdup(secure_peer_name));
  tsi_result result = c->InitHandshakerFactory(pem_root_certs, root_store);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    return nullptr;
  }
  return c;
}

/* handshaker */

typedef struct {
  void (*func)(void* arg, grpc_endpoint* endpoint);
  void* arg;
  grpc_core::RefCountedPtr<grpc_core::HandshakeManager> handshake_mgr;
} on_done_closure;

static void on_handshake_done(void* arg, grpc_error* error) {
  auto* args = static_cast<grpc_core::HandshakerArgs*>(arg);
  on_done_closure* c = static_cast<on_done_closure*>(args->user_data);
  if (error != GRPC_ERROR_NONE) {
    const char* msg = grpc_error_string(error);
    gpr_log(GPR_ERROR, "Secure transport setup failed: %s", msg);

    c->func(c->arg, nullptr);
  } else {
    grpc_channel_args_destroy(args->args);
    grpc_slice_buffer_destroy_internal(args->read_buffer);
    gpr_free(args->read_buffer);
    c->func(c->arg, args->endpoint);
  }
  delete c;
}

static void ssl_handshake(void* arg, grpc_endpoint* tcp, const char* host,
                          grpc_millis deadline,
                          void (*on_done)(void* arg, grpc_endpoint* endpoint)) {
  auto* c = new on_done_closure();
  const char* pem_root_certs =
      grpc_core::DefaultSslRootStore::GetPemRootCerts();
  const tsi_ssl_root_certs_store* root_store =
      grpc_core::DefaultSslRootStore::GetRootStore();
  if (root_store == nullptr) {
    gpr_log(GPR_ERROR, "Could not get default pem root certs.");
    on_done(arg, nullptr);
    gpr_free(c);
    return;
  }
  c->func = on_done;
  c->arg = arg;
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      httpcli_ssl_channel_security_connector_create(
          pem_root_certs, root_store, host,
          static_cast<grpc_core::HandshakerArgs*>(arg)->args);

  GPR_ASSERT(sc != nullptr);
  grpc_arg channel_arg = grpc_security_connector_to_arg(sc.get());
  grpc_channel_args args = {1, &channel_arg};
  c->handshake_mgr = grpc_core::MakeRefCounted<grpc_core::HandshakeManager>();
  grpc_core::HandshakerRegistry::AddHandshakers(
      grpc_core::HANDSHAKER_CLIENT, &args, /*interested_parties=*/nullptr,
      c->handshake_mgr.get());
  c->handshake_mgr->DoHandshake(tcp, /*channel_args=*/nullptr, deadline,
                                /*acceptor=*/nullptr, on_handshake_done,
                                /*user_data=*/c);
  sc.reset(DEBUG_LOCATION, "httpcli");
}

const grpc_httpcli_handshaker grpc_httpcli_ssl = {"https", ssl_handshake};
