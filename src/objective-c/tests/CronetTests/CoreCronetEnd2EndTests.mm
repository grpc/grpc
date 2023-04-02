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
#include <grpc/support/log.h>
#include "src/core/lib/gprpp/crash.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"

#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#import <Cronet/Cronet.h>
#include <grpc/grpc_cronet.h>

#import "../ConfigureCronet.h"

static void process_auth_failure(void *state, grpc_auth_context *ctx, const grpc_metadata *md,
                                 size_t md_count, grpc_process_auth_metadata_done_cb cb,
                                 void *user_data) {
  GPR_ASSERT(state == nullptr);
  cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
}

class CronetFixture final : public SecureFixture {
 private:
  grpc_channel_credentials *MakeClientCreds(const grpc_core::ChannelArgs &args) override {
    grpc_core::Crash("unreachable");
  }
  grpc_server_credentials *MakeServerCreds(const grpc_core::ChannelArgs &args) override {
    grpc_ssl_pem_key_cert_pair pem_cert_key_pair = {test_server1_key, test_server1_cert};
    grpc_server_credentials *ssl_creds =
        grpc_ssl_server_credentials_create(nullptr, &pem_cert_key_pair, 1, 0, nullptr);
    if (args.Contains(FAIL_AUTH_CHECK_SERVER_ARG_NAME)) {
      grpc_auth_metadata_processor processor = {process_auth_failure, nullptr, nullptr};
      grpc_server_credentials_set_auth_metadata_processor(ssl_creds, processor);
    }
    return ssl_creds;
  }
  grpc_channel *MakeClient(const grpc_core::ChannelArgs &args) override {
    stream_engine *cronetEngine = [Cronet getGlobalEngine];
    return grpc_cronet_secure_channel_create(cronetEngine, localaddr().c_str(), args.ToC().get(),
                                             nullptr);
  }
};

/* All test configurations */

static CoreTestConfiguration configs[] = {
    {"chttp2/simple_ssl_fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION | FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS, nullptr,
     [](const grpc_core::ChannelArgs & /*client_args*/,
        const grpc_core::ChannelArgs & /*server_args*/) {
       return std::make_unique<CronetFixture>();
     }},
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

  char *argv[] = {(char *)"CoreCronetEnd2EndTests"};
  int argc = 1;
  grpc_test_init(&argc, argv);
  grpc_end2end_tests_pre_init();

  /* Set the SSL roots env var. */
  roots_file = gpr_tmpfile("chttp2_simple_ssl_fullstack_test", &roots_filename);
  GPR_ASSERT(roots_filename != nullptr);
  GPR_ASSERT(roots_file != nullptr);
  GPR_ASSERT(fwrite(test_root_cert, 1, roots_size, roots_file) == roots_size);
  fclose(roots_file);
  grpc_core::ConfigVars::Overrides overrides;
  overrides.default_ssl_roots_file_path = roots_filename;
  grpc_core::ConfigVars::SetOverrides(overrides);

  grpc_init();

  configureCronet(/*enable_netlog=*/false);
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
  char *argv[] = {(char *)"h2_ssl", test_case};
  for (int i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(2, argv, configs[i]);
  }
}

// TODO(mxyan): Use NSStringFromSelector(_cmd) to acquire test name from the
// test case method name, so that bodies of test cases can stay identical
- (void)testAuthorityNotSupported {
  [self testIndividualCase:(char *)"authority_not_supported"];
}

- (void)testBadHostname {
  [self testIndividualCase:(char *)"bad_hostname"];
}

- (void)testBinaryMetadata {
  [self testIndividualCase:(char *)"binary_metadata"];
}

- (void)testCallCreds {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"call_creds"];
}

- (void)testCancelAfterAccept {
  [self testIndividualCase:(char *)"cancel_after_accept"];
}

- (void)testCancelAfterClientDone {
  [self testIndividualCase:(char *)"cancel_after_client_done"];
}

- (void)testCancelAfterInvoke {
  [self testIndividualCase:(char *)"cancel_after_invoke"];
}

- (void)testCancelAfterRoundTrip {
  [self testIndividualCase:(char *)"cancel_after_round_trip"];
}

- (void)testCancelBeforeInvoke {
  [self testIndividualCase:(char *)"cancel_before_invoke"];
}

- (void)testCancelInAVacuum {
  [self testIndividualCase:(char *)"cancel_in_a_vacuum"];
}

- (void)testCancelWithStatus {
  [self testIndividualCase:(char *)"cancel_with_status"];
}

- (void)testCompressedPayload {
  [self testIndividualCase:(char *)"compressed_payload"];
}

- (void)testConnectivity {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"connectivity"];
}

- (void)testDefaultHost {
  [self testIndividualCase:(char *)"default_host"];
}

- (void)testDisappearingServer {
  [self testIndividualCase:(char *)"disappearing_server"];
}

- (void)testEmptyBatch {
  [self testIndividualCase:(char *)"empty_batch"];
}

- (void)testFilterCausesClose {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"filter_causes_close"];
}

- (void)testGracefulServerShutdown {
  [self testIndividualCase:(char *)"graceful_server_shutdown"];
}

- (void)testHighInitialSeqno {
  [self testIndividualCase:(char *)"high_initial_seqno"];
}

- (void)testHpackSize {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"hpack_size"];
}

- (void)testIdempotentRequest {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"idempotent_request"];
}

- (void)testInvokeLargeRequest {
  // NOT SUPPORTED (frame size)
  // [self testIndividualCase:(char *)"invoke_large_request"];
}

- (void)testLargeMetadata {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"large_metadata"];
}

- (void)testMaxConcurrentStreams {
  [self testIndividualCase:(char *)"max_concurrent_streams"];
}

- (void)testMaxMessageLength {
  // NOT SUPPORTED (close_error)
  // [self testIndividualCase:(char *)"max_message_length"];
}

- (void)testNegativeDeadline {
  [self testIndividualCase:(char *)"negative_deadline"];
}

- (void)testNoOp {
  [self testIndividualCase:(char *)"no_op"];
}

- (void)testPayload {
  [self testIndividualCase:(char *)"payload"];
}

- (void)testPing {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"ping"];
}

- (void)testPingPongStreaming {
  [self testIndividualCase:(char *)"ping_pong_streaming"];
}

- (void)testRegisteredCall {
  [self testIndividualCase:(char *)"registered_call"];
}

- (void)testRequestWithFlags {
  // NOT SUPPORTED
  // [self testIndividualCase:(char *)"request_with_flags"];
}

- (void)testRequestWithPayload {
  [self testIndividualCase:(char *)"request_with_payload"];
}

- (void)testServerFinishesRequest {
  [self testIndividualCase:(char *)"server_finishes_request"];
}

- (void)testServerStreaming {
  [self testIndividualCase:(char *)"server_streaming"];
}

- (void)testShutdownFinishesCalls {
  [self testIndividualCase:(char *)"shutdown_finishes_calls"];
}

- (void)testShutdownFinishesTags {
  [self testIndividualCase:(char *)"shutdown_finishes_tags"];
}

- (void)testSimpleDelayedRequest {
  [self testIndividualCase:(char *)"simple_delayed_request"];
}

- (void)testSimpleMetadata {
  [self testIndividualCase:(char *)"simple_metadata"];
}

- (void)testSimpleRequest {
  [self testIndividualCase:(char *)"simple_request"];
}

- (void)testStreamingErrorResponse {
  [self testIndividualCase:(char *)"streaming_error_response"];
}

- (void)testTrailingMetadata {
  [self testIndividualCase:(char *)"trailing_metadata"];
}

@end
