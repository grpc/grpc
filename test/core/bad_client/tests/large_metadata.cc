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

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

// The large-metadata headers that we're adding for this test are not
// actually appended to this in a single string, since the string would
// be longer than the C99 string literal limit.  Instead, we dynamically
// construct it by adding the large headers one at a time.

#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_PREFIX_STR                       \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"     /* settings frame */              \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* headers: generated from        \
                                            large_metadata.headers in this \
                                            directory */                   \
  "\x00\x00\x00\x04\x01\x00\x00\x00\x00"                                   \
  "\x00"                                                                   \
  "5{\x01\x05\x00\x00\x00\x01"                                             \
  "\x10\x05:path\x08/foo/bar"                                              \
  "\x10\x07:scheme\x04http"                                                \
  "\x10\x07:method\x04POST"                                                \
  "\x10\x0a:authority\x09localhost"                                        \
  "\x10\x0c"                                                               \
  "content-type\x10"                                                       \
  "application/grpc"                                                       \
  "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"                  \
  "\x10\x02te\x08trailers"                                                 \
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
#define PFX_TOO_MUCH_METADATA_FROM_CLIENT_PAYLOAD_SIZE          \
  ((sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_PREFIX_STR) - 1) + \
   (NUM_HEADERS * PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_SIZE) + 1)

#define PFX_TOO_MUCH_METADATA_FROM_SERVER_STR                                              \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" /* settings frame: sets                               \
                                        MAX_HEADER_LIST_SIZE to 8K */                      \
  "\x00\x00\x06\x04\x00\x00\x00\x00\x00\x00\x06\x00\x00\x20\x00" /* headers:               \
                                                                    generated              \
                                                                    from                   \
                                                                    simple_request.headers \
                                                                    in this                \
                                                                    directory              \
                                                                    */                     \
  "\x00\x00\x00\x04\x01\x00\x00\x00\x00"                                                   \
  "\x00\x00\xc9\x01\x04\x00\x00\x00\x01"                                                   \
  "\x10\x05:path\x08/foo/bar"                                                              \
  "\x10\x07:scheme\x04http"                                                                \
  "\x10\x07:method\x04POST"                                                                \
  "\x10\x0a:authority\x09localhost"                                                        \
  "\x10\x0c"                                                                               \
  "content-type\x10"                                                                       \
  "application/grpc"                                                                       \
  "\x10\x14grpc-accept-encoding\x15"                                                       \
  "deflate,identity,gzip"                                                                  \
  "\x10\x02te\x08trailers"                                                                 \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"

static void* tag(intptr_t t) { return (void*)t; }

static void server_verifier(grpc_server* server, grpc_completion_queue* cq,
                            void* registered_method) {
  grpc_call_error error;
  grpc_call* s;
  grpc_call_details call_details;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.host, "localhost"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo/bar"));

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
}

static void server_verifier_sends_too_much_metadata(grpc_server* server,
                                                    grpc_completion_queue* cq,
                                                    void* registered_method) {
  grpc_call_error error;
  grpc_call* s;
  grpc_call_details call_details;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.host, "localhost"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo/bar"));

  const size_t metadata_value_size = 8 * 1024;
  grpc_metadata meta;
  meta.key = grpc_slice_from_static_string("key");
  meta.value = grpc_slice_malloc(metadata_value_size);
  memset(GRPC_SLICE_START_PTR(meta.value), 'a', metadata_value_size);

  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_INITIAL_METADATA;
  op.data.send_initial_metadata.count = 1;
  op.data.send_initial_metadata.metadata = &meta;
  op.flags = 0;
  op.reserved = nullptr;
  error = grpc_call_start_batch(s, &op, 1, tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(102), 0);  // Operation fails.
  cq_verify(cqv);

  grpc_slice_unref(meta.value);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
}

static bool client_validator(grpc_slice_buffer* incoming) {
  for (size_t i = 0; i < incoming->count; ++i) {
    const char* s = (const char*)GRPC_SLICE_START_PTR(incoming->slices[i]);
    char* hex = gpr_dump(s, GRPC_SLICE_LENGTH(incoming->slices[i]),
                         GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_INFO, "RESPONSE SLICE %" PRIdPTR ": %s", i, hex);
    gpr_free(hex);
  }

  // Get last frame from incoming slice buffer.
  grpc_slice_buffer last_frame_buffer;
  grpc_slice_buffer_init(&last_frame_buffer);
  grpc_slice_buffer_trim_end(incoming, 13, &last_frame_buffer);
  GPR_ASSERT(last_frame_buffer.count == 1);
  grpc_slice last_frame = last_frame_buffer.slices[0];

  const uint8_t* p = GRPC_SLICE_START_PTR(last_frame);
  bool success =
      // Length == 4
      *p++ != 0 || *p++ != 0 || *p++ != 4 ||
      // Frame type (RST_STREAM)
      *p++ != 3 ||
      // Flags
      *p++ != 0 ||
      // Stream ID.
      *p++ != 0 || *p++ != 0 || *p++ != 0 || *p++ != 1 ||
      // Payload (error code)
      *p++ == 0 || *p++ == 0 || *p++ == 0 || *p == 0 || *p == 11;

  if (!success) {
    gpr_log(GPR_INFO, "client expected RST_STREAM frame, not found");
  }

  grpc_slice_buffer_destroy(&last_frame_buffer);
  return success;
}

int main(int argc, char** argv) {
  int i;

  grpc_test_init(argc, argv);

  // Test sending more metadata than the server will accept.
  gpr_strvec headers;
  gpr_strvec_init(&headers);
  for (i = 0; i < NUM_HEADERS; ++i) {
    char* str;
    gpr_asprintf(&str, "%s%02d%s",
                 PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_START_STR, i,
                 PFX_TOO_MUCH_METADATA_FROM_CLIENT_HEADER_END_STR);
    gpr_strvec_add(&headers, str);
  }
  size_t headers_len;
  const char* client_headers = gpr_strvec_flatten(&headers, &headers_len);
  gpr_strvec_destroy(&headers);
  char client_payload[PFX_TOO_MUCH_METADATA_FROM_CLIENT_PAYLOAD_SIZE] =
      PFX_TOO_MUCH_METADATA_FROM_CLIENT_PREFIX_STR;
  memcpy(
      client_payload + sizeof(PFX_TOO_MUCH_METADATA_FROM_CLIENT_PREFIX_STR) - 1,
      client_headers, headers_len);
  GRPC_RUN_BAD_CLIENT_TEST(server_verifier, client_validator, client_payload,
                           0);
  gpr_free((void*)client_headers);

  // Test sending more metadata than the client will accept.
  GRPC_RUN_BAD_CLIENT_TEST(server_verifier_sends_too_much_metadata,
                           client_validator,
                           PFX_TOO_MUCH_METADATA_FROM_SERVER_STR, 0);

  return 0;
}
