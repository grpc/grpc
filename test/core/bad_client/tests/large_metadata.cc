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

#include "test/core/bad_client/bad_client.h"

#include <string.h>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

// The large-metadata headers that we're adding for this test are not
// actually appended to this in a single string, since the string would
// be longer than the C99 string literal limit.  Instead, we dynamically
// construct it by adding the large headers one at a time.

/* headers: generated from  large_metadata.headers in this directory */
#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_REQUEST         \
  "\x00\x00\x00\x04\x01\x00\x00\x00\x00"                  \
  "\x00"                                                  \
  "5{\x01\x05\x00\x00\x00\x01"                            \
  "\x10\x05:path\x08/foo/bar"                             \
  "\x10\x07:scheme\x04http"                               \
  "\x10\x07:method\x04POST"                               \
  "\x10\x0a:authority\x09localhost"                       \
  "\x10\x0c"                                              \
  "content-type\x10"                                      \
  "application/grpc"                                      \
  "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip" \
  "\x10\x02te\x08trailers"                                \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"

// Each large-metadata header is constructed from these start and end
// strings, with a two-digit number in between.
#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_START_STR "\x10\x0duser-header"
#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_END_STR                   \
  "~aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

// The size of each large-metadata header string.
#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_SIZE                     \
  ((sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_START_STR) - 1) + 2 + \
   (sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_END_STR) - 1))

// The number of headers we're adding and the total size of the client
// payload.
#define NUM_HEADERS 46
#define TOO_MUCH_METADATA_FROM_CLIENT_REQUEST_SIZE           \
  ((sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_REQUEST) - 1) + \
   (NUM_HEADERS * PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_SIZE) + 1)

int main(int argc, char** argv) {
  int i;
  grpc_init();
  grpc::testing::TestEnvironment env(argc, argv);

  // Test sending more metadata than the server will accept.
  std::vector<std::string> headers;
  for (i = 0; i < NUM_HEADERS; ++i) {
    headers.push_back(absl::StrFormat(
        "%s%02d%s", PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_START_STR, i,
        PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_END_STR));
  }
  std::string client_headers = absl::StrJoin(headers, "");
  char client_payload[TOO_MUCH_METADATA_FROM_CLIENT_REQUEST_SIZE] =
      PFX_TOO_MUCH_METADATA_FROM_CLIENT_REQUEST;
  memcpy(client_payload + sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_REQUEST) - 1,
         client_headers.data(), client_headers.size());
  grpc_bad_client_arg args[2];
  args[0] = connection_preface_arg;
  args[1].client_validator = rst_stream_client_validator;
  args[1].client_payload = client_payload;
  args[1].client_payload_length = sizeof(client_payload) - 1;

  grpc_run_bad_client_test(server_verifier_request_call, args, 2, 0);

  grpc_shutdown();
  return 0;
}
