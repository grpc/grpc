//
//
// Copyright 2018 gRPC authors.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h"
#include "test/core/util/fuzzer_util.h"

using grpc_core::testing::grpc_fuzzer_get_next_byte;
using grpc_core::testing::grpc_fuzzer_get_next_string;
using grpc_core::testing::input_stream;

// Logging
bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

// Add a random number of target service accounts to client options.
static void read_target_service_accounts(
    input_stream* inp, grpc_alts_credentials_options* options) {
  size_t n = grpc_fuzzer_get_next_byte(inp);
  for (size_t i = 0; i < n; i++) {
    char* service_account = grpc_fuzzer_get_next_string(inp, nullptr);
    if (service_account != nullptr) {
      grpc_alts_credentials_client_options_add_target_service_account(
          options, service_account);
      gpr_free(service_account);
    }
  }
  // Added to improve code coverage.
  grpc_alts_credentials_client_options_add_target_service_account(options,
                                                                  nullptr);
  grpc_alts_credentials_client_options_add_target_service_account(
      nullptr, "this is service account");
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  input_stream inp = {data, data + size};
  grpc_init();
  bool is_on_gcp = grpc_alts_is_running_on_gcp();
  while (inp.cur != inp.end) {
    bool enable_untrusted_alts = grpc_fuzzer_get_next_byte(&inp) & 0x01;
    char* handshaker_service_url =
        grpc_fuzzer_get_next_byte(&inp) & 0x01
            ? grpc_fuzzer_get_next_string(&inp, nullptr)
            : nullptr;
    if (grpc_fuzzer_get_next_byte(&inp) & 0x01) {
      // Test ALTS channel credentials.
      grpc_alts_credentials_options* options =
          grpc_alts_credentials_client_options_create();
      read_target_service_accounts(&inp, options);
      grpc_channel_credentials* cred = grpc_alts_credentials_create_customized(
          options, handshaker_service_url, enable_untrusted_alts);
      if (!enable_untrusted_alts && !is_on_gcp) {
        GPR_ASSERT(cred == nullptr);
      } else {
        GPR_ASSERT(cred != nullptr);
      }
      grpc_channel_credentials_release(cred);
      grpc_alts_credentials_options_destroy(options);
    } else {
      // Test ALTS server credentials.
      grpc_alts_credentials_options* options =
          grpc_alts_credentials_server_options_create();
      grpc_server_credentials* cred =
          grpc_alts_server_credentials_create_customized(
              options, handshaker_service_url, enable_untrusted_alts);
      if (!enable_untrusted_alts && !is_on_gcp) {
        GPR_ASSERT(cred == nullptr);
      } else {
        GPR_ASSERT(cred != nullptr);
      }
      grpc_server_credentials_release(cred);
      grpc_alts_credentials_options_destroy(options);
    }
    gpr_free(handshaker_service_url);
  }
  grpc_shutdown();
  return 0;
}
