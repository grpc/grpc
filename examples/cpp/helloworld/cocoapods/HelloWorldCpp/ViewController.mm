/*
 *
 * Copyright 2018 gRPC authors.
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

#import "ViewController.h"
#import <grpcpp/grpcpp.h>

#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/generic/generic_stub.h>

static void* tag(int i) { return (void*)(intptr_t)i; }

// Serialized Proto bytes of Hello World example
const uint8_t kMessage[] = {0x0A, 0x0B, 0x4F, 0x62, 0x6A, 0x65, 0x63,
                            0x74, 0x69, 0x76, 0x65, 0x2D, 0x43};

@interface ViewController ()

@end

@implementation ViewController {
  grpc::CompletionQueue cq_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  // Setup call stub
  std::shared_ptr<grpc::Channel> channel =
      CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  generic_stub_.reset(new grpc::GenericStub(channel));

  const std::string kMethodName("/helloworld.Greeter/SayHello");
  void* got_tag;
  bool ok;

  grpc::ClientContext cli_ctx;
  std::unique_ptr<grpc::GenericClientAsyncReaderWriter> call =
      generic_stub_->Call(&cli_ctx, kMethodName, &cq_, tag(1));
  cq_.Next(&got_tag, &ok);
  if (!ok || got_tag != tag(1)) {
    NSLog(@"Failed to create call.");
    abort();
  }
  grpc::Slice send_slice = grpc::Slice(kMessage, sizeof(kMessage) / sizeof(kMessage[0]));
  std::unique_ptr<grpc::ByteBuffer> send_buffer(new grpc::ByteBuffer(&send_slice, 1));
  call->Write(*send_buffer, tag(2));
  cq_.Next(&got_tag, &ok);
  if (!ok || got_tag != tag(2)) {
    NSLog(@"Failed to send message.");
    abort();
  }
  grpc::ByteBuffer recv_buffer;
  call->Read(&recv_buffer, tag(3));
  cq_.Next(&got_tag, &ok);
  if (!ok || got_tag != tag(3)) {
    NSLog(@"Failed to receive message.");
    abort();
  }

  grpc::Status status;
  call->Finish(&status, tag(4));
  cq_.Next(&got_tag, &ok);
  if (!ok || got_tag != tag(4)) {
    NSLog(@"Failed to finish call.");
    abort();
  }
  if (!status.ok()) {
    NSLog(@"Received unsuccessful status code: %d", status.error_code());
    abort();
  }
  std::vector<grpc::Slice> slices;
  recv_buffer.Dump(&slices);
  NSString* recvBytes = [[NSString alloc] init];
  for (auto slice : slices) {
    auto p = slice.begin();
    while (p != slice.end()) {
      recvBytes = [recvBytes stringByAppendingString:[NSString stringWithFormat:@"%02x ", *p]];
      p++;
    }
  }
  NSLog(@"Hello World succeeded.\nReceived bytes: %@\n"
         "Expected bytes: 0a 11 48 65 6c 6c 6f 20 4f 62 6a 65 63 74 69 76 65 2d 43",
        recvBytes);
}

@end
