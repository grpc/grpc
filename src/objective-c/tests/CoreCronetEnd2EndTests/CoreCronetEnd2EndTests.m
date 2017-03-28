/*
 *
 * Copyright 2015, Google Inc.
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

/*
 * This test file is derived from fixture h2_ssl.c in core end2end test
 * (test/core/end2end/fixture/h2_ssl.c). The structure of the fixture is
 * preserved as much as possible
 *
 * This fixture creates a server full stack using chttp2 and a client
 * full stack using Cronet. End-to-end tests are run against this
 * configuration
 *
 */

#import <XCTest/XCTest.h>
#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#import <Cronet/Cronet.h>
#include <grpc/grpc_cronet.h>

typedef struct fullstack_secure_fixture_data {
  char *localaddr;
} fullstack_secure_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_secure_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data *ffd =
      gpr_malloc(sizeof(fullstack_secure_fixture_data));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "127.0.0.1", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

static void process_auth_failure(void *state, grpc_auth_context *ctx,
                                 const grpc_metadata *md, size_t md_count,
                                 grpc_process_auth_metadata_done_cb cb,
                                 void *user_data) {
  GPR_ASSERT(state == NULL);
  cb(user_data, NULL, 0, NULL, 0, GRPC_STATUS_UNAUTHENTICATED, NULL);
}

static void cronet_init_client_secure_fullstack(grpc_end2end_test_fixture *f,
                                                grpc_channel_args *client_args,
                                                stream_engine *cronetEngine) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  f->client = grpc_cronet_secure_channel_create(cronetEngine, ffd->localaddr,
                                                client_args, NULL);
  GPR_ASSERT(f->client != NULL);
}

static void chttp2_init_server_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args,
    grpc_server_credentials *server_creds) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  f->server = grpc_server_create(server_args, NULL);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(f->server, ffd->localaddr,
                                               server_creds));
  grpc_server_credentials_release(server_creds);
  grpc_server_start(f->server);
}

static void chttp2_tear_down_secure_fullstack(grpc_end2end_test_fixture *f) {
  fullstack_secure_fixture_data *ffd = f->fixture_data;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

static void cronet_init_client_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *client_args) {
  grpc_exec_ctx ctx = GRPC_EXEC_CTX_INIT;
  stream_engine *cronetEngine = [Cronet getGlobalEngine];

  grpc_channel_args *new_client_args = grpc_channel_args_copy(client_args);
  cronet_init_client_secure_fullstack(f, new_client_args, cronetEngine);
  grpc_channel_args_destroy(&ctx, new_client_args);
  grpc_exec_ctx_finish(&ctx);
}

static int fail_server_auth_check(grpc_channel_args *server_args) {
  size_t i;
  if (server_args == NULL) return 0;
  for (i = 0; i < server_args->num_args; i++) {
    if (strcmp(server_args->args[i].key, FAIL_AUTH_CHECK_SERVER_ARG_NAME) ==
        0) {
      return 1;
    }
  }
  return 0;
}

static void chttp2_init_server_simple_ssl_secure_fullstack(
    grpc_end2end_test_fixture *f, grpc_channel_args *server_args) {
  grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials *ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_cert_key_pair, 1, 0, NULL);
  if (fail_server_auth_check(server_args)) {
    grpc_auth_metadata_processor processor = {process_auth_failure, NULL, NULL};
    grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
  }
  chttp2_init_server_secure_fullstack(f, server_args, ssl_creds);
}

/* All test configurations */

static grpc_end2end_test_config configs[] = {
    {"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS,
     chttp2_create_fixture_secure_fullstack,
     cronet_init_client_simple_ssl_secure_fullstack,
     chttp2_init_server_simple_ssl_secure_fullstack,
     chttp2_tear_down_secure_fullstack},
};

static char *roots_filename;

@interface CoreCronetEnd2EndTests : XCTestCase

@end

@implementation CoreCronetEnd2EndTests

// The setUp() function is run before the test cases run and only run once
+ (void)setUp {
  [super setUp];

  FILE *roots_file;
  size_t roots_size = strlen(test_root_cert);

  char *argv[] = {"CoreCronetEnd2EndTests"};
  grpc_test_init(1, argv);
  grpc_end2end_tests_pre_init();

  /* Set the SSL roots env var. */
  roots_file = gpr_tmpfile("chttp2_simple_ssl_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != NULL);
  GPR_ASSERT(roots_file != NULL);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  gpr_setenv(GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR, roots_filename);

  grpc_init();

  [Cronet setHttp2Enabled:YES];
  [Cronet enableTestCertVerifierForTesting];
  NSURL *url = [[[NSFileManager defaultManager]
      URLsForDirectory:NSDocumentDirectory
             inDomains:NSUserDomainMask] lastObject];
  NSLog(@"Documents directory: %@", url);
  [Cronet start];
  [Cronet startNetLogToFile:@"cronet_netlog.json" logBytes:YES];
}

// The tearDown() function is run after all test cases finish running
+ (void)tearDown {
  grpc_shutdown();

  /* Cleanup. */
  remove(roots_filename);
  gpr_free(roots_filename);

  [super tearDown];
}

- (void)testIndividualCase:(char *)test_case {
  char *argv[] = {"h2_ssl", test_case};

  for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(sizeof(argv) / sizeof(argv[0]), argv, configs[i]);
  }
}

// TODO(mxyan): Use NSStringFromSelector(_cmd) to acquire test name from the
// test case method name, so that bodies of test cases can stay identical
- (void)testAuthorityNotSupported {
  [self testIndividualCase:"authority_not_supported"];
}

- (void)testBadHostname {
  [self testIndividualCase:"bad_hostname"];
}

- (void)testBinaryMetadata {
  // NOT SUPPORTED
  //[self testIndividualCase:"binary_metadata"];
}

- (void)testCallCreds {
  // NOT SUPPORTED
  // [self testIndividualCase:"call_creds"];
}

- (void)testCancelAfterAccept {
  [self testIndividualCase:"cancel_after_accept"];
}

- (void)testCancelAfterClientDone {
  [self testIndividualCase:"cancel_after_client_done"];
}

- (void)testCancelAfterInvoke {
  [self testIndividualCase:"cancel_after_invoke"];
}

- (void)testCancelBeforeInvoke {
  [self testIndividualCase:"cancel_before_invoke"];
}

- (void)testCancelInAVacuum {
  [self testIndividualCase:"cancel_in_a_vacuum"];
}

- (void)testCancelWithStatus {
  [self testIndividualCase:"cancel_with_status"];
}

- (void)testCompressedPayload {
  [self testIndividualCase:"compressed_payload"];
}

- (void)testConnectivity {
  // NOT SUPPORTED
  // [self testIndividualCase:"connectivity"];
}

- (void)testDefaultHost {
  [self testIndividualCase:"default_host"];
}

- (void)testDisappearingServer {
  [self testIndividualCase:"disappearing_server"];
}

- (void)testEmptyBatch {
  [self testIndividualCase:"empty_batch"];
}

- (void)testFilterCausesClose {
  // NOT SUPPORTED
  // [self testIndividualCase:"filter_causes_close"];
}

- (void)testGracefulServerShutdown {
  [self testIndividualCase:"graceful_server_shutdown"];
}

- (void)testHighInitialSeqno {
  [self testIndividualCase:"high_initial_seqno"];
}

- (void)testHpackSize {
  // NOT SUPPORTED
  // [self testIndividualCase:"hpack_size"];
}

- (void)testIdempotentRequest {
  // NOT SUPPORTED
  // [self testIndividualCase:"idempotent_request"];
}

- (void)testInvokeLargeRequest {
  // NOT SUPPORTED (frame size)
  // [self testIndividualCase:"invoke_large_request"];
}

- (void)testLargeMetadata {
  // NOT SUPPORTED
  // [self testIndividualCase:"large_metadata"];
}

- (void)testMaxConcurrentStreams {
  [self testIndividualCase:"max_concurrent_streams"];
}

- (void)testMaxMessageLength {
  // NOT SUPPORTED (close_error)
  // [self testIndividualCase:"max_message_length"];
}

- (void)testNegativeDeadline {
  [self testIndividualCase:"negative_deadline"];
}

- (void)testNetworkStatusChange {
  [self testIndividualCase:"network_status_change"];
}

- (void)testNoOp {
  [self testIndividualCase:"no_op"];
}

- (void)testPayload {
  [self testIndividualCase:"payload"];
}

- (void)testPing {
  // NOT SUPPORTED
  // [self testIndividualCase:"ping"];
}

- (void)testPingPongStreaming {
  [self testIndividualCase:"ping_pong_streaming"];
}

- (void)testRegisteredCall {
  [self testIndividualCase:"registered_call"];
}

- (void)testRequestWithFlags {
  // NOT SUPPORTED
  // [self testIndividualCase:"request_with_flags"];
}

- (void)testRequestWithPayload {
  [self testIndividualCase:"request_with_payload"];
}

- (void)testServerFinishesRequest {
  [self testIndividualCase:"server_finishes_request"];
}

- (void)testShutdownFinishesCalls {
  [self testIndividualCase:"shutdown_finishes_calls"];
}

- (void)testShutdownFinishesTags {
  [self testIndividualCase:"shutdown_finishes_tags"];
}

- (void)testSimpleDelayedRequest {
  [self testIndividualCase:"simple_delayed_request"];
}

- (void)testSimpleMetadata {
  [self testIndividualCase:"simple_metadata"];
}

- (void)testSimpleRequest {
  [self testIndividualCase:"simple_request"];
}

- (void)testStreamingErrorResponse {
  [self testIndividualCase:"streaming_error_response"];
}

- (void)testTrailingMetadata {
  [self testIndividualCase:"trailing_metadata"];
}

@end
