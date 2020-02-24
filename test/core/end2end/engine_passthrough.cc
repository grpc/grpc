/*
 *
 * Copyright 2020 gRPC authors.
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

// This is a sample openSSL engine which tests the openSSL
// engine plugability with gRPC.
// This sample engine expects KeyId to be actual PEM encoded
// key itself and just calls standard openSSL functions.

#include <openssl/bio.h>
#include <openssl/engine.h>
#include <openssl/pem.h>

#ifndef OPENSSL_IS_BORINGSSL

#include <stdio.h>
#include <string.h>

extern "C" {
static const char engine_id[] = "libengine_passthrough";
static const char engine_name[] = "A passthrough engine for private keys";
static int e_passthrough_idx = -1;

static int e_passthrough_init(ENGINE* e) {
  if (e_passthrough_idx < 0) {
    e_passthrough_idx = ENGINE_get_ex_new_index(0, NULL, NULL, NULL, 0);
    if (e_passthrough_idx < 0) return 0;
  }
  return 1;
}

EVP_PKEY* e_passthrough_load_privkey(ENGINE* eng, const char* key_id,
                                     UI_METHOD* ui_method,
                                     void* callback_data) {
  EVP_PKEY* pkey = NULL;
  BIO* pem = BIO_new_mem_buf((void*)key_id, (int)(strlen(key_id)));
  if (pem == NULL) return NULL;
  pkey = PEM_read_bio_PrivateKey(pem, NULL, NULL, (void*)"");
  BIO_free(pem);
  return pkey;
}

int passthrough_bind_helper(ENGINE* e, const char* id) {
  if (id && strcmp(id, engine_id)) {
    return 0;
  }
  if (!ENGINE_set_id(e, engine_id) || !ENGINE_set_name(e, engine_name) ||
      !ENGINE_set_flags(e, ENGINE_FLAGS_NO_REGISTER_ALL) ||
      !ENGINE_set_init_function(e, e_passthrough_init) ||
      !ENGINE_set_load_privkey_function(e, e_passthrough_load_privkey)) {
    return 0;
  }
  return 1;
}

IMPLEMENT_DYNAMIC_BIND_FN(passthrough_bind_helper)
IMPLEMENT_DYNAMIC_CHECK_FN()
}
#endif  // OPENSSL_IS_BORINGSSL
