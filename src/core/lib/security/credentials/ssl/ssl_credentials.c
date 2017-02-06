/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"

#include <string.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/http_client_filter.h"
#include "src/core/lib/surface/api_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

//
// Utils
//

static void ssl_copy_key_material(const char *input, unsigned char **output,
                                  size_t *output_size) {
  *output_size = strlen(input);
  *output = gpr_malloc(*output_size);
  memcpy(*output, input, *output_size);
}

//
// SSL Channel Credentials.
//

static void ssl_destruct(grpc_exec_ctx *exec_ctx,
                         grpc_channel_credentials *creds) {
  grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
  if (c->config.pem_private_key != NULL) gpr_free(c->config.pem_private_key);
  if (c->config.pem_cert_chain != NULL) gpr_free(c->config.pem_cert_chain);
}

static grpc_security_status ssl_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_channel_credentials *creds,
    grpc_call_credentials *call_creds, const char *target,
    const grpc_channel_args *args, grpc_channel_security_connector **sc,
    grpc_channel_args **new_args) {
  grpc_ssl_credentials *c = (grpc_ssl_credentials *)creds;
  grpc_security_status status = GRPC_SECURITY_OK;
  size_t i = 0;
  const char *overridden_target_name = NULL;
  grpc_arg new_arg;

  for (i = 0; args && i < args->num_args; i++) {
    grpc_arg *arg = &args->args[i];
    if (strcmp(arg->key, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == 0 &&
        arg->type == GRPC_ARG_STRING) {
      overridden_target_name = arg->value.string;
      break;
    }
  }
  status = grpc_ssl_channel_security_connector_create(
      exec_ctx, call_creds, &c->config, target, overridden_target_name, sc);
  if (status != GRPC_SECURITY_OK) {
    return status;
  }
  new_arg.type = GRPC_ARG_STRING;
  new_arg.key = GRPC_ARG_HTTP2_SCHEME;
  new_arg.value.string = "https";
  *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  return status;
}

static grpc_channel_credentials_vtable ssl_vtable = {
    ssl_destruct, ssl_create_security_connector, NULL};

static void ssl_build_config(const char *pem_root_certs,
                             grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
                             grpc_ssl_config *config) {
  if (pem_root_certs != NULL) {
    ssl_copy_key_material(pem_root_certs, &config->pem_root_certs,
                          &config->pem_root_certs_size);
  }
  if (pem_key_cert_pair != NULL) {
    GPR_ASSERT(pem_key_cert_pair->private_key != NULL);
    GPR_ASSERT(pem_key_cert_pair->cert_chain != NULL);
    ssl_copy_key_material(pem_key_cert_pair->private_key,
                          &config->pem_private_key,
                          &config->pem_private_key_size);
    ssl_copy_key_material(pem_key_cert_pair->cert_chain,
                          &config->pem_cert_chain,
                          &config->pem_cert_chain_size);
  }
}

grpc_channel_credentials *grpc_ssl_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
    void *reserved) {
  grpc_ssl_credentials *c = gpr_malloc(sizeof(grpc_ssl_credentials));
  GRPC_API_TRACE(
      "grpc_ssl_credentials_create(pem_root_certs=%s, "
      "pem_key_cert_pair=%p, "
      "reserved=%p)",
      3, (pem_root_certs, pem_key_cert_pair, reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(grpc_ssl_credentials));
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  c->base.vtable = &ssl_vtable;
  gpr_ref_init(&c->base.refcount, 1);
  ssl_build_config(pem_root_certs, pem_key_cert_pair, &c->config);
  return &c->base;
}

//
// SSL Server Credentials.
//

static void ssl_server_destruct(grpc_exec_ctx *exec_ctx,
                                grpc_server_credentials *creds) {
  grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
  size_t i;
  for (i = 0; i < c->config.num_key_cert_pairs; i++) {
    if (c->config.pem_private_keys[i] != NULL) {
      gpr_free(c->config.pem_private_keys[i]);
    }
    if (c->config.pem_cert_chains[i] != NULL) {
      gpr_free(c->config.pem_cert_chains[i]);
    }
  }
  if (c->config.pem_private_keys != NULL) gpr_free(c->config.pem_private_keys);
  if (c->config.pem_private_keys_sizes != NULL) {
    gpr_free(c->config.pem_private_keys_sizes);
  }
  if (c->config.pem_cert_chains != NULL) gpr_free(c->config.pem_cert_chains);
  if (c->config.pem_cert_chains_sizes != NULL) {
    gpr_free(c->config.pem_cert_chains_sizes);
  }
  if (c->config.pem_root_certs != NULL) gpr_free(c->config.pem_root_certs);
}

static grpc_security_status ssl_server_create_security_connector(
    grpc_exec_ctx *exec_ctx, grpc_server_credentials *creds,
    grpc_server_security_connector **sc) {
  grpc_ssl_server_credentials *c = (grpc_ssl_server_credentials *)creds;
  return grpc_ssl_server_security_connector_create(exec_ctx, &c->config, sc);
}

static grpc_server_credentials_vtable ssl_server_vtable = {
    ssl_server_destruct, ssl_server_create_security_connector};

static void ssl_build_server_config(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_config *config) {
  size_t i;
  config->client_certificate_request = client_certificate_request;
  if (pem_root_certs != NULL) {
    ssl_copy_key_material(pem_root_certs, &config->pem_root_certs,
                          &config->pem_root_certs_size);
  }
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != NULL);
    config->pem_private_keys =
        gpr_malloc(num_key_cert_pairs * sizeof(unsigned char *));
    config->pem_cert_chains =
        gpr_malloc(num_key_cert_pairs * sizeof(unsigned char *));
    config->pem_private_keys_sizes =
        gpr_malloc(num_key_cert_pairs * sizeof(size_t));
    config->pem_cert_chains_sizes =
        gpr_malloc(num_key_cert_pairs * sizeof(size_t));
  }
  config->num_key_cert_pairs = num_key_cert_pairs;
  for (i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != NULL);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != NULL);
    ssl_copy_key_material(pem_key_cert_pairs[i].private_key,
                          &config->pem_private_keys[i],
                          &config->pem_private_keys_sizes[i]);
    ssl_copy_key_material(pem_key_cert_pairs[i].cert_chain,
                          &config->pem_cert_chains[i],
                          &config->pem_cert_chains_sizes[i]);
  }
}

grpc_server_credentials *grpc_ssl_server_credentials_create(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void *reserved) {
  return grpc_ssl_server_credentials_create_ex(
      pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs,
      force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
      reserved);
}

grpc_server_credentials *grpc_ssl_server_credentials_create_ex(
    const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void *reserved) {
  grpc_ssl_server_credentials *c =
      gpr_malloc(sizeof(grpc_ssl_server_credentials));
  GRPC_API_TRACE(
      "grpc_ssl_server_credentials_create_ex("
      "pem_root_certs=%s, pem_key_cert_pairs=%p, num_key_cert_pairs=%lu, "
      "client_certificate_request=%d, reserved=%p)",
      5, (pem_root_certs, pem_key_cert_pairs, (unsigned long)num_key_cert_pairs,
          client_certificate_request, reserved));
  GPR_ASSERT(reserved == NULL);
  memset(c, 0, sizeof(grpc_ssl_server_credentials));
  c->base.type = GRPC_CHANNEL_CREDENTIALS_TYPE_SSL;
  gpr_ref_init(&c->base.refcount, 1);
  c->base.vtable = &ssl_server_vtable;
  ssl_build_server_config(pem_root_certs, pem_key_cert_pairs,
                          num_key_cert_pairs, client_certificate_request,
                          &c->config);
  return &c->base;
}
