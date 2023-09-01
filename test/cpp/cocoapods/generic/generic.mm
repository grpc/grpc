/*
 *
 * Copyright 2017 gRPC authors.
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

#import <XCTest/XCTest.h>

#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/slice.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using std::chrono::system_clock;
using namespace grpc;

void* tag(int i) { return (void*)(intptr_t)i; }

static grpc_slice merge_slices(grpc_slice* slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  uint8_t* cursor;
  grpc_slice out;

  for (i = 0; i < nslices; i++) {
    len += GRPC_SLICE_LENGTH(slices[i]);
  }

  out = grpc_slice_malloc(len);
  cursor = GRPC_SLICE_START_PTR(out);

  for (i = 0; i < nslices; i++) {
    memcpy(cursor, GRPC_SLICE_START_PTR(slices[i]), GRPC_SLICE_LENGTH(slices[i]));
    cursor += GRPC_SLICE_LENGTH(slices[i]);
  }

  return out;
}

int byte_buffer_eq_string(ByteBuffer* bb, const char* str) {
  int res;

  std::vector<Slice> slices;
  bb->Dump(&slices);
  grpc_slice* c_slices = new grpc_slice[slices.size()];
  for (int i = 0; i < slices.size(); i++) {
    c_slices[i] = slices[i].c_slice();
  }
  grpc_slice a = merge_slices(c_slices, slices.size());
  grpc_slice b = grpc_slice_from_copied_string(str);
  res = (GRPC_SLICE_LENGTH(a) == GRPC_SLICE_LENGTH(b)) &&
        (0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b), GRPC_SLICE_LENGTH(a)));
  grpc_slice_unref(a);
  grpc_slice_unref(b);
  for (int i = 0; i < slices.size(); i++) {
    grpc_slice_unref(c_slices[i]);
  }
  delete[] c_slices;

  return res;
}

@interface GenericTest : XCTestCase

@end

@implementation GenericTest {
  std::string server_host_;
  CompletionQueue cli_cq_;
  std::unique_ptr<ServerCompletionQueue> srv_cq_;
  std::unique_ptr<GenericStub> generic_stub_;
  std::unique_ptr<Server> server_;
  AsyncGenericService generic_service_;
  std::ostringstream server_address_;
}

- (void)verify_ok:(grpc::CompletionQueue*)cq i:(int)i expect_ok:(bool)expect_ok {
  bool ok;
  void* got_tag;
  XCTAssertTrue(cq->Next(&got_tag, &ok));
  XCTAssertEqual(expect_ok, ok);
  XCTAssertEqual(tag(i), got_tag);
}

- (void)server_ok:(int)i {
  [self verify_ok:srv_cq_.get() i:i expect_ok:true];
}
- (void)client_ok:(int)i {
  [self verify_ok:&cli_cq_ i:i expect_ok:true];
}
- (void)server_fail:(int)i {
  [self verify_ok:srv_cq_.get() i:i expect_ok:false];
}
- (void)client_fail:(int)i {
  [self verify_ok:&cli_cq_ i:i expect_ok:false];
}

- (void)setUp {
  [super setUp];

  server_host_ = "localhost";
  int port = grpc_pick_unused_port_or_die();
  server_address_ << server_host_ << ":" << port;
  // Setup server
  ServerBuilder builder;
  builder.AddListeningPort(server_address_.str(), InsecureServerCredentials());
  builder.RegisterAsyncGenericService(&generic_service_);
  // Include a second call to RegisterAsyncGenericService to make sure that
  // we get an error in the log, since it is not allowed to have 2 async
  // generic services
  builder.RegisterAsyncGenericService(&generic_service_);
  srv_cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();
}

- (void)tearDown {
  // Put teardown code here. This method is called after the invocation of each test method in the
  // class.
  server_->Shutdown();
  void* ignored_tag;
  bool ignored_ok;
  cli_cq_.Shutdown();
  srv_cq_->Shutdown();
  while (cli_cq_.Next(&ignored_tag, &ignored_ok))
    ;
  while (srv_cq_->Next(&ignored_tag, &ignored_ok))
    ;
  [super tearDown];
}

- (void)ResetStub {
  std::shared_ptr<Channel> channel =
      CreateChannel(server_address_.str(), InsecureChannelCredentials());
  generic_stub_.reset(new GenericStub(channel));
}

- (void)SendRpc:(int)num_rpcs {
  [self SendRpc:num_rpcs check_deadline:false deadline:gpr_inf_future(GPR_CLOCK_MONOTONIC)];
}

- (void)SendRpc:(int)num_rpcs check_deadline:(bool)check_deadline deadline:(gpr_timespec)deadline {
  const std::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
  for (int i = 0; i < num_rpcs; i++) {
    Status recv_status;

    ClientContext cli_ctx;
    GenericServerContext srv_ctx;
    GenericServerAsyncReaderWriter stream(&srv_ctx);

    // The string needs to be long enough to test heap-based slice.
    /*send_request.set_message("Hello world. Hello world. Hello world.");*/

    if (check_deadline) {
      cli_ctx.set_deadline(deadline);
    }

    std::unique_ptr<GenericClientAsyncReaderWriter> call =
        generic_stub_->Call(&cli_ctx, kMethodName, &cli_cq_, tag(1));
    [self client_ok:1];
    Slice send_slice = Slice("hello world", 11);
    std::unique_ptr<ByteBuffer> send_buffer =
        std::unique_ptr<ByteBuffer>(new ByteBuffer(&send_slice, 1));
    call->Write(*send_buffer, tag(2));
    // Send ByteBuffer can be destroyed after calling Write.
    send_buffer.reset();
    [self client_ok:2];
    call->WritesDone(tag(3));
    [self client_ok:3];

    generic_service_.RequestCall(&srv_ctx, &stream, srv_cq_.get(), srv_cq_.get(), tag(4));

    [self verify_ok:srv_cq_.get() i:4 expect_ok:true];
    XCTAssertEqual(server_host_, srv_ctx.host().substr(0, server_host_.length()));
    XCTAssertEqual(kMethodName, srv_ctx.method());

    if (check_deadline) {
      XCTAssertTrue(gpr_time_similar(deadline, srv_ctx.raw_deadline(),
                                     gpr_time_from_millis(1000, GPR_TIMESPAN)));
    }

    ByteBuffer recv_buffer;
    stream.Read(&recv_buffer, tag(5));
    [self server_ok:5];
    XCTAssertTrue(byte_buffer_eq_string(&recv_buffer, "hello world"));

    send_buffer = std::unique_ptr<ByteBuffer>(new ByteBuffer(recv_buffer));
    stream.Write(*send_buffer, tag(6));
    send_buffer.reset();
    [self server_ok:6];

    stream.Finish(Status::OK, tag(7));
    [self server_ok:7];

    recv_buffer.Clear();
    call->Read(&recv_buffer, tag(8));
    [self client_ok:8];
    XCTAssertTrue(byte_buffer_eq_string(&recv_buffer, "hello world"));

    call->Finish(&recv_status, tag(9));
    [self client_ok:9];

    XCTAssertTrue(recv_status.ok());
  }
}

- (void)testSimpleRpc {
  [self ResetStub];
  [self SendRpc:1];
}

- (void)testSequentialRpcs {
  [self ResetStub];
  [self SendRpc:10];
}

+ (void)setUp {
  grpc_test_init(NULL, NULL);
}

@end
