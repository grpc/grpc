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

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/ssl/ssl_security_connector.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl_transport_security.h"

/* handshaker */

struct on_done_closure {
  void (*func)(void* arg, grpc_endpoint* endpoint);
  void* arg;
  grpc_core::RefCountedPtr<grpc_core::HandshakeManager> handshake_mgr;
};
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
  c->func = on_done;
  c->arg = arg;

  grpc_ssl_config config;
  config.pem_key_cert_pair = nullptr;
  config.pem_root_certs =
      const_cast<char*>(grpc_core::DefaultSslRootStore::GetPemRootCerts());
  config.skip_alpn_check = true;
  memset(&config.verify_options, 0, sizeof(verify_peer_options));
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      grpc_ssl_channel_security_connector_create(
          /*channel_creds=*/nullptr,
          /*request_metadata_creds=*/nullptr, &config, host,
          /*overridden_target_name=*/nullptr,
          /*ssl_session_cache=*/nullptr);

  GPR_ASSERT(sc != nullptr);
  grpc_arg channel_arg = grpc_security_connector_to_arg(sc.get());
  grpc_channel_args args = {1, &channel_arg};
  c->handshake_mgr = grpc_core::MakeRefCounted<grpc_core::HandshakeManager>();
  grpc_core::HandshakerRegistry::AddHandshakers(
      grpc_core::HANDSHAKER_CLIENT, &args,
      /*interested_parties=*/nullptr, c->handshake_mgr.get());
  c->handshake_mgr->DoHandshake(tcp, /*channel_args=*/nullptr, deadline,
                                /*acceptor=*/nullptr, on_handshake_done,
                                /*user_data=*/c);
  sc.reset(DEBUG_LOCATION, "httpcli");
}

const grpc_httpcli_handshaker grpc_httpcli_ssl = {"https", ssl_handshake};
