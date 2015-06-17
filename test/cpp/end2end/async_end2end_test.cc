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

#include <memory>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.grpc.pb.h"
#include "test/cpp/util/echo.grpc.pb.h"
#include <grpc++/async_unary_call.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include <grpc++/time.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {

void* tag(int i) { return (void*)(gpr_intptr) i; }

class Verifier {
 public:
  Verifier& Expect(int i, bool expect_ok) {
    expectations_[tag(i)] = expect_ok;
    return *this;
  }
  void Verify(CompletionQueue *cq) {
    GPR_ASSERT(!expectations_.empty());
    while (!expectations_.empty()) {
      bool ok;
      void* got_tag;
      EXPECT_TRUE(cq->Next(&got_tag, &ok));
      auto it = expectations_.find(got_tag);
      EXPECT_TRUE(it != expectations_.end());
      EXPECT_EQ(it->second, ok);
      expectations_.erase(it);
    }
  }
  void Verify(CompletionQueue *cq, std::chrono::system_clock::time_point deadline) {
    if (expectations_.empty()) {
      bool ok;
      void *got_tag;
      EXPECT_EQ(cq->AsyncNext(&got_tag, &ok, deadline), CompletionQueue::TIMEOUT);
    } else {
      while (!expectations_.empty()) {
        bool ok;
        void *got_tag;
        EXPECT_EQ(cq->AsyncNext(&got_tag, &ok, deadline), CompletionQueue::GOT_EVENT);
        auto it = expectations_.find(got_tag);
        EXPECT_TRUE(it != expectations_.end());
        EXPECT_EQ(it->second, ok);
        expectations_.erase(it);
      }
    }
  }

 private:
  std::map<void*, bool> expectations_;
};

class AsyncEnd2endTest : public ::testing::Test {
 protected:
  AsyncEnd2endTest() {}

  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(), grpc::InsecureServerCredentials());
    builder.RegisterAsyncService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    cq_->Shutdown();
    while (cq_->Next(&ignored_tag, &ignored_ok))
      ;
  }

  void ResetStub() {
    std::shared_ptr<ChannelInterface> channel = CreateChannel(
        server_address_.str(), InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel));
  }

  void SendRpc(int num_rpcs) {
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      ServerContext srv_ctx;
      grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

      send_request.set_message("Hello");
      std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
          stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

      service_.RequestEcho(&srv_ctx, &recv_request, &response_writer,
                           cq_.get(), cq_.get(), tag(2));

      Verifier().Expect(2, true).Verify(cq_.get());
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      response_writer.Finish(send_response, Status::OK, tag(3));
      Verifier().Expect(3, true).Verify(cq_.get());

      response_reader->Finish(&recv_response, &recv_status, tag(4));
      Verifier().Expect(4, true).Verify(cq_.get());

      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  grpc::cpp::test::util::TestService::AsyncService service_;
  std::ostringstream server_address_;
};

TEST_F(AsyncEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(1);
}

TEST_F(AsyncEnd2endTest, SequentialRpcs) {
  ResetStub();
  SendRpc(10);
}

// Test a simple RPC using the async version of Next
TEST_F(AsyncEnd2endTest, AsyncNextRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  std::chrono::system_clock::time_point time_now(
      std::chrono::system_clock::now());
  std::chrono::system_clock::time_point time_limit(
      std::chrono::system_clock::now() + std::chrono::seconds(10));
  Verifier().Verify(cq_.get(), time_now);
  Verifier().Verify(cq_.get(), time_now);

  service_.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                       cq_.get(), tag(2));

  Verifier().Expect(2, true).Verify(cq_.get(), time_limit);
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));
  Verifier().Expect(3, true).Verify(cq_.get(), std::chrono::system_clock::time_point::max());

  response_reader->Finish(&recv_response, &recv_status, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get(), std::chrono::system_clock::time_point::max());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// Two pings and a final pong.
TEST_F(AsyncEnd2endTest, SimpleClientStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  std::unique_ptr<ClientAsyncWriter<EchoRequest> > cli_stream(
      stub_->AsyncRequestStream(&cli_ctx, &recv_response, cq_.get(), tag(1)));

  service_.RequestRequestStream(&srv_ctx, &srv_stream, cq_.get(),
                                cq_.get(), tag(2));

  Verifier().Expect(2, true).Expect(1, true).Verify(cq_.get());

  cli_stream->Write(send_request, tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  cli_stream->Write(send_request, tag(5));
  Verifier().Expect(5, true).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(6));
  Verifier().Expect(6, true).Verify(cq_.get());

  EXPECT_EQ(send_request.message(), recv_request.message());
  cli_stream->WritesDone(tag(7));
  Verifier().Expect(7, true).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(8));
  Verifier().Expect(8, false).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_stream.Finish(send_response, Status::OK, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(10));
  Verifier().Expect(10, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

// One ping, two pongs.
TEST_F(AsyncEnd2endTest, SimpleServerStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  std::unique_ptr<ClientAsyncReader<EchoResponse> > cli_stream(
      stub_->AsyncResponseStream(&cli_ctx, send_request, cq_.get(), tag(1)));

  service_.RequestResponseStream(&srv_ctx, &recv_request, &srv_stream,
                                 cq_.get(), cq_.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  cli_stream->Read(&recv_response, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Write(send_response, tag(5));
  Verifier().Expect(5, true).Verify(cq_.get());

  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  srv_stream.Finish(Status::OK, tag(7));
  Verifier().Expect(7, true).Verify(cq_.get());

  cli_stream->Read(&recv_response, tag(8));
  Verifier().Expect(8, false).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// One ping, one pong.
TEST_F(AsyncEnd2endTest, SimpleBidiStreaming) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  ServerContext srv_ctx;
  ServerAsyncReaderWriter<EchoResponse, EchoRequest> srv_stream(&srv_ctx);

  send_request.set_message("Hello");
  std::unique_ptr<ClientAsyncReaderWriter<EchoRequest, EchoResponse> >
      cli_stream(stub_->AsyncBidiStream(&cli_ctx, cq_.get(), tag(1)));

  service_.RequestBidiStream(&srv_ctx, &srv_stream, cq_.get(),
                             cq_.get(), tag(2));

  Verifier().Expect(1, true).Expect(2, true).Verify(cq_.get());

  cli_stream->Write(send_request, tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  srv_stream.Write(send_response, tag(5));
  Verifier().Expect(5, true).Verify(cq_.get());

  cli_stream->Read(&recv_response, tag(6));
  Verifier().Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  Verifier().Expect(7, true).Verify(cq_.get());

  srv_stream.Read(&recv_request, tag(8));
  Verifier().Expect(8, false).Verify(cq_.get());

  srv_stream.Finish(Status::OK, tag(9));
  Verifier().Expect(9, true).Verify(cq_.get());

  cli_stream->Finish(&recv_status, tag(10));
  Verifier().Expect(10, true).Verify(cq_.get());

  EXPECT_TRUE(recv_status.ok());
}

// Metadata tests
TEST_F(AsyncEnd2endTest, ClientInitialMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");
  cli_ctx.AddMetadata(meta1.first, meta1.second);
  cli_ctx.AddMetadata(meta2.first, meta2.second);

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  service_.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                       cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  auto client_initial_metadata = srv_ctx.client_metadata();
  EXPECT_EQ(meta1.second, client_initial_metadata.find(meta1.first)->second);
  EXPECT_EQ(meta2.second, client_initial_metadata.find(meta2.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), client_initial_metadata.size());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(3));

  Verifier().Expect(3, true).Verify(cq_.get());

  response_reader->Finish(&recv_response, &recv_status, tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_F(AsyncEnd2endTest, ServerInitialMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  service_.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                       cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  srv_ctx.AddInitialMetadata(meta1.first, meta1.second);
  srv_ctx.AddInitialMetadata(meta2.first, meta2.second);
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  response_reader->ReadInitialMetadata(tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());
  auto server_initial_metadata = cli_ctx.GetServerInitialMetadata();
  EXPECT_EQ(meta1.second, server_initial_metadata.find(meta1.first)->second);
  EXPECT_EQ(meta2.second, server_initial_metadata.find(meta2.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), server_initial_metadata.size());

  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(5));
  Verifier().Expect(5, true).Verify(cq_.get());

  response_reader->Finish(&recv_response, &recv_status, tag(6));
  Verifier().Expect(6, true).Verify(cq_.get());

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
}

TEST_F(AsyncEnd2endTest, ServerTrailingMetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2("key2", "val2");

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  service_.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                       cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());

  send_response.set_message(recv_request.message());
  srv_ctx.AddTrailingMetadata(meta1.first, meta1.second);
  srv_ctx.AddTrailingMetadata(meta2.first, meta2.second);
  response_writer.Finish(send_response, Status::OK, tag(4));

  Verifier().Expect(4, true).Verify(cq_.get());

  response_reader->Finish(&recv_response, &recv_status, tag(5));
  Verifier().Expect(5, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  auto server_trailing_metadata = cli_ctx.GetServerTrailingMetadata();
  EXPECT_EQ(meta1.second, server_trailing_metadata.find(meta1.first)->second);
  EXPECT_EQ(meta2.second, server_trailing_metadata.find(meta2.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), server_trailing_metadata.size());
}

TEST_F(AsyncEnd2endTest, MetadataRpc) {
  ResetStub();

  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;

  ClientContext cli_ctx;
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);

  send_request.set_message("Hello");
  std::pair<grpc::string, grpc::string> meta1("key1", "val1");
  std::pair<grpc::string, grpc::string> meta2(
      "key2-bin",
      grpc::string("\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc",
		   13));
  std::pair<grpc::string, grpc::string> meta3("key3", "val3");
  std::pair<grpc::string, grpc::string> meta6(
      "key4-bin",
      grpc::string("\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d",
		   14));
  std::pair<grpc::string, grpc::string> meta5("key5", "val5");
  std::pair<grpc::string, grpc::string> meta4(
      "key6-bin",
      grpc::string("\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee",
		   15));

  cli_ctx.AddMetadata(meta1.first, meta1.second);
  cli_ctx.AddMetadata(meta2.first, meta2.second);

  std::unique_ptr<ClientAsyncResponseReader<EchoResponse> > response_reader(
      stub_->AsyncEcho(&cli_ctx, send_request, cq_.get()));

  service_.RequestEcho(&srv_ctx, &recv_request, &response_writer, cq_.get(),
                       cq_.get(), tag(2));
  Verifier().Expect(2, true).Verify(cq_.get());
  EXPECT_EQ(send_request.message(), recv_request.message());
  auto client_initial_metadata = srv_ctx.client_metadata();
  EXPECT_EQ(meta1.second, client_initial_metadata.find(meta1.first)->second);
  EXPECT_EQ(meta2.second, client_initial_metadata.find(meta2.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), client_initial_metadata.size());

  srv_ctx.AddInitialMetadata(meta3.first, meta3.second);
  srv_ctx.AddInitialMetadata(meta4.first, meta4.second);
  response_writer.SendInitialMetadata(tag(3));
  Verifier().Expect(3, true).Verify(cq_.get());
  response_reader->ReadInitialMetadata(tag(4));
  Verifier().Expect(4, true).Verify(cq_.get());
  auto server_initial_metadata = cli_ctx.GetServerInitialMetadata();
  EXPECT_EQ(meta3.second, server_initial_metadata.find(meta3.first)->second);
  EXPECT_EQ(meta4.second, server_initial_metadata.find(meta4.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), server_initial_metadata.size());

  send_response.set_message(recv_request.message());
  srv_ctx.AddTrailingMetadata(meta5.first, meta5.second);
  srv_ctx.AddTrailingMetadata(meta6.first, meta6.second);
  response_writer.Finish(send_response, Status::OK, tag(5));

  Verifier().Expect(5, true).Verify(cq_.get());

  response_reader->Finish(&recv_response, &recv_status, tag(6));
  Verifier().Expect(6, true).Verify(cq_.get());
  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());
  auto server_trailing_metadata = cli_ctx.GetServerTrailingMetadata();
  EXPECT_EQ(meta5.second, server_trailing_metadata.find(meta5.first)->second);
  EXPECT_EQ(meta6.second, server_trailing_metadata.find(meta6.first)->second);
  EXPECT_EQ(static_cast<size_t>(2), server_trailing_metadata.size());
}
}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
