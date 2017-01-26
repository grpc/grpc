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

#import <XCTest/XCTest.h>
#import <sys/socket.h>
#import <netinet/in.h>

#import <Cronet/Cronet.h>
#import <grpc/support/host_port.h>
#import <grpc/grpc_cronet.h>
#import <grpc/grpc.h>
#import "test/core/end2end/cq_verifier.h"
#import "test/core/util/port.h"

#import <grpc/support/alloc.h>
#import <grpc/support/log.h>

#import "src/core/lib/channel/channel_args.h"
#import "src/core/lib/support/env.h"
#import "src/core/lib/support/string.h"
#import "src/core/lib/support/tmpfile.h"
#import "test/core/util/test_config.h"

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}


@interface CronetUnitTests : XCTestCase

@end

@implementation CronetUnitTests

+ (void)setUp {
  [super setUp];

/***  FILE *roots_file;
  size_t roots_size = strlen(test_root_cert);*/

  char *argv[] = {"CoreCronetEnd2EndTests"};
  grpc_test_init(1, argv);

  grpc_init();

  [Cronet setHttp2Enabled:YES];
  NSURL *url = [[[NSFileManager defaultManager]
                 URLsForDirectory:NSDocumentDirectory
                 inDomains:NSUserDomainMask] lastObject];
  NSLog(@"Documents directory: %@", url);
  [Cronet start];
  [Cronet startNetLogToFile:@"Documents/cronet_netlog.json" logBytes:YES];
}

+ (void)tearDown {
  grpc_shutdown();

  [super tearDown];
}

- (void)testInternalError {
  grpc_call *c;
  grpc_slice request_payload_slice =
  grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload =
  grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_metadata meta_c[2] = {
    {"key1", "val1", 4, 0, {{NULL, NULL, NULL, NULL}}},
    {"key2", "val2", 4, 0, {{NULL, NULL, NULL, NULL}}}};

  int port = grpc_pick_unused_port_or_die();
  char *addr;
  gpr_join_host_port(&addr, "127.0.0.1", port);
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  cronet_engine *cronetEngine = [Cronet getGlobalEngine];
  grpc_channel *client = grpc_cronet_secure_channel_create(cronetEngine, addr,
                                                           NULL, NULL);

  cq_verifier *cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  char *details = NULL;
  size_t details_capacity = 0;

  c = grpc_channel_create_call(
                               client, NULL, GRPC_PROPAGATE_DEFAULTS, cq, "/foo",
                               NULL, deadline, NULL);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 2;
  op->data.send_initial_metadata.metadata = meta_c;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), (void*)1, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    int sl = socket(AF_INET, SOCK_STREAM, 0);
    GPR_ASSERT(sl >= 0);
    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    s_addr.sin_port = htons(port);
    bind(sl, (struct sockaddr*)&s_addr, sizeof(s_addr));
    listen(sl, 5);
    int s = accept(sl, NULL, NULL);
    sleep(1);
    close(s);
    close(sl);
  });

  CQ_EXPECT_COMPLETION(cqv, (void*)1, 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);

  gpr_free(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_destroy(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(response_payload_recv);
  
  grpc_channel_destroy(client);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);
}

@end
