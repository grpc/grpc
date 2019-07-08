/*
 *
 * Copyright 2019 gRPC authors.
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

#import <Cronet/Cronet.h>
#import <RemoteTestCpp/messages.grpc.pb.h>
#import <RemoteTestCpp/test.grpc.pb.h>
#import <grpc/grpc_cronet.h>
#import <grpcpp/create_channel.h>
#import <grpcpp/impl/codegen/client_context.h>
#import <grpcpp/security/credentials.h>

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  [Cronet setHttp2Enabled:YES];
  [Cronet setQuicEnabled:YES];
  [Cronet start];
  stream_engine *cronetEngine = [Cronet getGlobalEngine];

  auto cronetChannelCredentials = grpc::CronetChannelCredentials(cronetEngine);
  grpc::ChannelArguments args;
  auto channel =
      grpc::CreateCustomChannel("grpc-test.sandbox.googleapis.com", cronetChannelCredentials, args);

  auto stub = grpc::testing::TestService::NewStub(channel);
  grpc::ClientContext context;
  grpc::testing::SimpleRequest request;
  request.mutable_payload()->set_body(std::string(10, '\0'));
  request.set_response_size(100);
  grpc::testing::SimpleResponse response;
  auto status = stub->UnaryCall(&context, request, &response);
  NSLog(@"Got response size: %ld success: %d", response.payload().body().length(), status.ok());
}

@end
