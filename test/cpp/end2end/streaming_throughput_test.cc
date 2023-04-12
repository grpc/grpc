//
//
// Copyright 2015 gRPC authors.
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

#include <time.h>

#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

const char* kLargeString =
    "("
    "To be, or not to be- that is the question:"
    "Whether 'tis nobler in the mind to suffer"
    "The slings and arrows of outrageous fortune"
    "Or to take arms against a sea of troubles,"
    "And by opposing end them. To die- to sleep-"
    "No more; and by a sleep to say we end"
    "The heartache, and the thousand natural shock"
    "That flesh is heir to. 'Tis a consummation"
    "Devoutly to be wish'd. To die- to sleep."
    "To sleep- perchance to dream: ay, there's the rub!"
    "For in that sleep of death what dreams may come"
    "When we have shuffled off this mortal coil,"
    "Must give us pause. There's the respect"
    "That makes calamity of so long life."
    "For who would bear the whips and scorns of time,"
    "Th' oppressor's wrong, the proud man's contumely,"
    "The pangs of despis'd love, the law's delay,"
    "The insolence of office, and the spurns"
    "That patient merit of th' unworthy takes,"
    "When he himself might his quietus make"
    "With a bare bodkin? Who would these fardels bear,"
    "To grunt and sweat under a weary life,"
    "But that the dread of something after death-"
    "The undiscover'd country, from whose bourn"
    "No traveller returns- puzzles the will,"
    "And makes us rather bear those ills we have"
    "Than fly to others that we know not of?"
    "Thus conscience does make cowards of us all,"
    "And thus the native hue of resolution"
    "Is sicklied o'er with the pale cast of thought,"
    "And enterprises of great pith and moment"
    "With this regard their currents turn awry"
    "And lose the name of action.- Soft you now!"
    "The fair Ophelia!- Nymph, in thy orisons"
    "Be all my sins rememb'red.";

namespace grpc {
namespace testing {

class TestServiceImpl : public grpc::testing::EchoTestService::Service {
 public:
  static void BidiStream_Sender(
      ServerReaderWriter<EchoResponse, EchoRequest>* stream,
      gpr_atm* should_exit) {
    EchoResponse response;
    response.set_message(kLargeString);
    while (gpr_atm_acq_load(should_exit) == gpr_atm{0}) {
      struct timespec tv = {0, 1000000};  // 1 ms
      struct timespec rem;
      // TODO (vpai): Mark this blocking
      while (nanosleep(&tv, &rem) != 0) {
        tv = rem;
      };

      stream->Write(response);
    }
  }

  // Only implement the one method we will be calling for brevity.
  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    gpr_atm should_exit;
    gpr_atm_rel_store(&should_exit, gpr_atm{0});

    std::thread sender(
        std::bind(&TestServiceImpl::BidiStream_Sender, stream, &should_exit));

    while (stream->Read(&request)) {
      struct timespec tv = {0, 3000000};  // 3 ms
      struct timespec rem;
      // TODO (vpai): Mark this blocking
      while (nanosleep(&tv, &rem) != 0) {
        tv = rem;
      };
    }
    gpr_atm_rel_store(&should_exit, gpr_atm{1});
    sender.join();
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

static void Drainer(ClientReaderWriter<EchoRequest, EchoResponse>* reader) {
  EchoResponse response;
  while (reader->Read(&response)) {
    // Just drain out the responses as fast as possible.
  }
}

TEST_F(End2endTest, StreamingThroughput) {
  ResetStub();
  grpc::ClientContext context;
  auto stream = stub_->BidiStream(&context);

  auto reader = stream.get();
  std::thread receiver(std::bind(Drainer, reader));

  for (int i = 0; i < 10000; i++) {
    EchoRequest request;
    request.set_message(kLargeString);
    ASSERT_TRUE(stream->Write(request));
    if (i % 1000 == 0) {
      gpr_log(GPR_INFO, "Send count = %d", i);
    }
  }
  stream->WritesDone();
  receiver.join();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
