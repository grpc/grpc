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

#include <mutex>
#include <thread>
#include <time.h>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/atm.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.grpc.pb.h"
#include "test/cpp/util/echo.grpc.pb.h"

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using std::chrono::system_clock;

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

class TestServiceImpl : public ::grpc::cpp::test::util::TestService::Service {
 public:
  static void BidiStream_Sender(
      ServerReaderWriter<EchoResponse, EchoRequest>* stream,
      gpr_atm* should_exit) {
    EchoResponse response;
    response.set_message(kLargeString);
    while (gpr_atm_acq_load(should_exit) == static_cast<gpr_atm>(0)) {
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
  Status BidiStream(ServerContext* context,
                    ServerReaderWriter<EchoResponse, EchoRequest>* stream)
      GRPC_OVERRIDE {
    EchoRequest request;
    gpr_atm should_exit;
    gpr_atm_rel_store(&should_exit, static_cast<gpr_atm>(0));

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
    gpr_atm_rel_store(&should_exit, static_cast<gpr_atm>(1));
    sender.join();
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::cpp::test::util::TestService::NewStub(channel);
  }

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
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
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
