/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"

#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

//
// SSL Channel Credentials.
//

void grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_ssl_pem_key_cert_pair* kp,
                                             size_t num_key_cert_pairs) {
  if (kp == nullptr) return;
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    gpr_free((void*)kp[i].private_key);
    gpr_free((void*)kp[i].cert_chain);
  }
  gpr_free(kp);
}

static void ssl_destruct(grpc_channel_credentials* creds) {
  grpc_ssl_credentials* c = (grpc_ssl_credentials*)creds;
  gpr_free(c->config.pem_root_certs);
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(c->config.pem_key_cert_pair, 1);
}

static grpc_security_status ssl_create_security_connector(
    grpc_channel_credentials* creds, grpc_call_credentials* call_creds,
    const char* target, const grpc_channel_args* args,
    grpc_channel_security_connector** sc, grpc_channel_args** new_args) {
  grpc_ssl_credentials* c = (grpc_ssl_credentials*)creds;
  grpc_security_status status = GRPC_SECURITY_OK;
  const char* overridden_target_name = nullptr;
  for (size_t i = 0; args && i < args->num_args; i++) {
    grpc_arg* arg = &args->args[i];
    if (strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == 0 &&
        arg->type == GRPC_ARG_STRING) {
      overridden_target_name = arg->value.string;
      break;
    }
  }
  status = grpc_ssl_channel_security_connector_create(
      creds, call_creds, &c->config, target, overridden_target_name, sc);
  if (status != GRPC_SECURITY_OK) {
    return status;
  }
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_HTTP2_SCHEME, (char*)"https");
  *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  return status;
}

static grpc_channel_credentials_vtable ssl_vtable = {
    ssl_destruct, ssl_create_security_connector, nullptr};

static void ssl_build_config(const char* pem_root_certs,
                             grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
                             grpc_ssl_config* config) {
  if (pem_root_certs != nullptr) {
    config->pem_root_certs = gpr_strdup(pem_root_certs);
  }
  if (pem_key_cert_pair != nullptr) {
    GPR_ASSERT(pem_key_cert_pair->private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pair->cert_chain != nullptr);
    config->pem_key_cert_pair = (tsi_ssl_pem_key_cert_pair*)gpr_zalloc(
        sizeof(tsi_ssl_pem_key_cert_pair));
    config->pem_key_cert_pair->cert_chain =
        gpr_strdup(pem_key_cert_pair->cert_chain);
    config->pem_key_cert_pair->private_key =
        gpr_strdup(pem_key_cert_pair->private_key);
  }
}

grpc_channel_credentials* grpc_ssl_credentials_create(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    void* reserved) {
  grpc_ssl_credentials* c =
      (grpc_ssl_credentials*)gpr_zalloc(sizeof(grpc_ssl_credentials));
  GRPC_API_TRACE(
      "grpc_ssl_credentials_create(pem_root_certs=%s, "
      "pem_key_cert_pair=%p, "
      "reserved=%p)",
      3, (pem_root_certs, pem_key_cert_pair, reserved));
  GPR_ASSERT(reserved == nullptr);
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  c->base.vtable = &ssl_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  ssl_build_config(pem_root_certs, pem_key_cert_pair, &c->config);
  return &c->base;
}

//
// SSL Server Credentials.
//

struct grpc_ssl_server_credentials_options {
  grpc_ssl_client_certificate_request_type client_certificate_request;
  grpc_ssl_server_certificate_config* certificate_config;
  grpc_ssl_server_certificate_config_fetcher* certificate_config_fetcher;
};

static void ssl_server_destruct(grpc_server_credentials* creds) {
  grpc_ssl_server_credentials* c = (grpc_ssl_server_credentials*)creds;
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(c->config.pem_key_cert_pairs,
                                          c->config.num_key_cert_pairs);
  gpr_free(c->config.pem_root_certs);
}

static grpc_security_status ssl_server_create_security_connector(
    grpc_server_credentials* creds, grpc_server_security_connector** sc) {
  return grpc_ssl_server_security_connector_create(creds, sc);
}

static grpc_server_credentials_vtable ssl_server_vtable = {
    ssl_server_destruct, ssl_server_create_security_connector};

tsi_ssl_pem_key_cert_pair* grpc_convert_grpc_to_tsi_cert_pairs(
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs) {
  tsi_ssl_pem_key_cert_pair* tsi_pairs = nullptr;
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != nullptr);
    tsi_pairs = (tsi_ssl_pem_key_cert_pair*)gpr_zalloc(
        num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != nullptr);
    tsi_pairs[i].cert_chain = gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    tsi_pairs[i].private_key = gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  return tsi_pairs;
}

static void ssl_build_server_config(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_config* config) {
  config->client_certificate_request = client_certificate_request;
  if (pem_root_certs != nullptr) {
    config->pem_root_certs = gpr_strdup(pem_root_certs);
  }
  config->pem_key_cert_pairs = grpc_convert_grpc_to_tsi_cert_pairs(
      pem_key_cert_pairs, num_key_cert_pairs);
  config->num_key_cert_pairs = num_key_cert_pairs;
}

grpc_ssl_server_certificate_config* grpc_ssl_server_certificate_config_create(
    const char* pem_root_certs,
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs) {
  grpc_ssl_server_certificate_config* config =
      (grpc_ssl_server_certificate_config*)gpr_zalloc(
          sizeof(grpc_ssl_server_certificate_config));
  if (pem_root_certs != nullptr) {
    config->pem_root_certs = gpr_strdup(pem_root_certs);
  }
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != nullptr);
    config->pem_key_cert_pairs = (grpc_ssl_pem_key_cert_pair*)gpr_zalloc(
        num_key_cert_pairs * sizeof(grpc_ssl_pem_key_cert_pair));
  }
  config->num_key_cert_pairs = num_key_cert_pairs;
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != nullptr);
    config->pem_key_cert_pairs[i].cert_chain =
        gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    config->pem_key_cert_pairs[i].private_key =
        gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  return config;
}

void grpc_ssl_server_certificate_config_destroy(
    grpc_ssl_server_certificate_config* config) {
  if (config == nullptr) return;
  for (size_t i = 0; i < config->num_key_cert_pairs; i++) {
    gpr_free((void*)config->pem_key_cert_pairs[i].private_key);
    gpr_free((void*)config->pem_key_cert_pairs[i].cert_chain);
  }
  gpr_free(config->pem_key_cert_pairs);
  gpr_free(config->pem_root_certs);
  gpr_free(config);
}

grpc_ssl_server_credentials_options*
grpc_ssl_server_credentials_create_options_using_config(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config* config) {
  grpc_ssl_server_credentials_options* options = nullptr;
  if (config == nullptr) {
    gpr_log(GPR_ERROR, "Certificate config must not be NULL.");
    goto done;
  }
  options = (grpc_ssl_server_credentials_options*)gpr_zalloc(
      sizeof(grpc_ssl_server_credentials_options));
  options->client_certificate_request = client_certificate_request;
  options->certificate_config = config;
done:
  return options;
}

grpc_ssl_server_credentials_options*
grpc_ssl_server_credentials_create_options_using_config_fetcher(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config_callback cb, void* user_data) {
  if (cb == nullptr) {
    gpr_log(GPR_ERROR, "Invalid certificate config callback parameter.");
    return nullptr;
  }

  grpc_ssl_server_certificate_config_fetcher* fetcher =
      (grpc_ssl_server_certificate_config_fetcher*)gpr_zalloc(
          sizeof(grpc_ssl_server_certificate_config_fetcher));
  fetcher->cb = cb;
  fetcher->user_data = user_data;

  grpc_ssl_server_credentials_options* options =
      (grpc_ssl_server_credentials_options*)gpr_zalloc(
          sizeof(grpc_ssl_server_credentials_options));
  options->client_certificate_request = client_certificate_request;
  options->certificate_config_fetcher = fetcher;

  return options;
}

grpc_server_credentials* grpc_ssl_server_credentials_create(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void* reserved) {
  return grpc_ssl_server_credentials_create_ex(
      pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs,
      force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
      reserved);
}

grpc_server_credentials* grpc_ssl_server_credentials_create_ex(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void* reserved) {
  GRPC_API_TRACE(
      "grpc_ssl_server_credentials_create_ex("
      "pem_root_certs=%s, pem_key_cert_pairs=%p, num_key_cert_pairs=%lu, "
      "client_certificate_request=%d, reserved=%p)",
      5,
      (pem_root_certs, pem_key_cert_pairs, (unsigned long)num_key_cert_pairs,
       client_certificate_request, reserved));
  GPR_ASSERT(reserved == nullptr);

  grpc_ssl_server_certificate_config* cert_config =
      grpc_ssl_server_certificate_config_create(
          pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs);
  grpc_ssl_server_credentials_options* options =
      grpc_ssl_server_credentials_create_options_using_config(
          client_certificate_request, cert_config);

  return grpc_ssl_server_credentials_create_with_options(options);
}

grpc_server_credentials* grpc_ssl_server_credentials_create_with_options(
    grpc_ssl_server_credentials_options* options) {
  grpc_server_credentials* retval = nullptr;
  grpc_ssl_server_credentials* c = nullptr;

  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid options trying to create SSL server credentials.");
    goto done;
  }

  if (options->certificate_config == nullptr &&
      options->certificate_config_fetcher == nullptr) {
    gpr_log(GPR_ERROR,
            "SSL server credentials options must specify either "
            "certificate config or fetcher.");
    goto done;
  } else if (options->certificate_config_fetcher != nullptr &&
             options->certificate_config_fetcher->cb == nullptr) {
    gpr_log(GPR_ERROR, "Certificate config fetcher callback must not be NULL.");
    goto done;
  }

  c = (grpc_ssl_server_credentials*)gpr_zalloc(
      sizeof(grpc_ssl_server_credentials));
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &ssl_server_vtable;

  if (options->certificate_config_fetcher != nullptr) {
    c->config.client_certificate_request = options->client_certificate_request;
    c->certificate_config_fetcher = *options->certificate_config_fetcher;
  } else {
    ssl_build_server_config(options->certificate_config->pem_root_certs,
                            options->certificate_config->pem_key_cert_pairs,
                            options->certificate_config->num_key_cert_pairs,
                            options->client_certificate_request, &c->config);
  }

  retval = &c->base;

done:
  grpc_ssl_server_credentials_options_destroy(options);
  return retval;
}

void grpc_ssl_server_credentials_options_destroy(
    grpc_ssl_server_credentials_options* o) {
  if (o == nullptr) return;
  gpr_free(o->certificate_config_fetcher);
  grpc_ssl_server_certificate_config_destroy(o->certificate_config);
  gpr_free(o);
}
