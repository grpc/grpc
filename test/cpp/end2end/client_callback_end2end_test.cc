//
//
// Copyright 2018 gRPC authors.
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

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/impl/proto_utils.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_callback.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/util/env.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/interceptors_util.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {
namespace {

enum class Protocol { INPROC, TCP };

class TestScenario {
 public:
  TestScenario(bool serve_callback, Protocol protocol, bool intercept,
               const std::string& creds_type)
      : callback_server(serve_callback),
        protocol(protocol),
        use_interceptors(intercept),
        credentials_type(creds_type) {}
  void Log() const;
  bool callback_server;
  Protocol protocol;
  bool use_interceptors;
  const std::string credentials_type;
};

std::ostream& operator<<(std::ostream& out, const TestScenario& scenario) {
  return out << "TestScenario{callback_server="
             << (scenario.callback_server ? "true" : "false") << ",protocol="
             << (scenario.protocol == Protocol::INPROC ? "INPROC" : "TCP")
             << ",intercept=" << (scenario.use_interceptors ? "true" : "false")
             << ",creds=" << scenario.credentials_type << "}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  VLOG(2) << out.str();
}

class ClientCallbackEnd2endTest
    : public ::testing::TestWithParam<TestScenario> {
 protected:
  ClientCallbackEnd2endTest() { GetParam().Log(); }

  void SetUp() override {
    ServerBuilder builder;

    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    // TODO(vjpai): Support testing of AuthMetadataProcessor

    if (GetParam().protocol == Protocol::TCP) {
      picked_port_ = grpc_pick_unused_port_or_die();
      server_address_ << "localhost:" << picked_port_;
      builder.AddListeningPort(server_address_.str(), server_creds);
    }
    if (!GetParam().callback_server) {
      builder.RegisterService(&service_);
    } else {
      builder.RegisterService(&callback_service_);
    }

    if (GetParam().use_interceptors) {
      std::vector<
          std::unique_ptr<experimental::ServerInterceptorFactoryInterface>>
          creators;
      // Add 20 phony server interceptors
      creators.reserve(20);
      for (auto i = 0; i < 20; i++) {
        creators.push_back(std::make_unique<PhonyInterceptorFactory>());
      }
      builder.experimental().SetInterceptorCreators(std::move(creators));
    }

    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetStub(
      std::unique_ptr<experimental::ClientInterceptorFactoryInterface>
          interceptor = nullptr) {
    ChannelArguments args;
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    auto interceptors = CreatePhonyClientInterceptors();
    if (interceptor != nullptr) interceptors.push_back(std::move(interceptor));
    switch (GetParam().protocol) {
      case Protocol::TCP:
        if (!GetParam().use_interceptors) {
          channel_ = grpc::CreateCustomChannel(server_address_.str(),
                                               channel_creds, args);
        } else {
          channel_ = CreateCustomChannelWithInterceptors(
              server_address_.str(), channel_creds, args,
              std::move(interceptors));
        }
        break;
      case Protocol::INPROC:
        if (!GetParam().use_interceptors) {
          channel_ = server_->InProcessChannel(args);
        } else {
          channel_ = server_->experimental().InProcessChannelWithInterceptors(
              args, std::move(interceptors));
        }
        break;
      default:
        assert(false);
    }
    stub_ = grpc::testing::EchoTestService::NewStub(channel_);
    generic_stub_ = std::make_unique<GenericStub>(channel_);
    PhonyInterceptor::Reset();
  }

  void TearDown() override {
    if (is_server_started_) {
      // Although we would normally do an explicit shutdown, the server
      // should also work correctly with just a destructor call. The regular
      // end2end test uses explicit shutdown, so let this one just do reset.
      server_.reset();
    }
    if (picked_port_ > 0) {
      grpc_recycle_unused_port(picked_port_);
    }
  }

  void SendRpcs(int num_rpcs, bool with_binary_metadata) {
    std::string test_string;
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest request;
      EchoResponse response;
      ClientContext cli_ctx;

      test_string += "Hello world. ";
      request.set_message(test_string);
      std::string val;
      if (with_binary_metadata) {
        request.mutable_param()->set_echo_metadata(true);
        char bytes[8] = {'\0', '\1', '\2', '\3',
                         '\4', '\5', '\6', static_cast<char>(i)};
        val = std::string(bytes, 8);
        cli_ctx.AddMetadata("custom-bin", val);
      }

      cli_ctx.set_compression_algorithm(GRPC_COMPRESS_GZIP);

      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      stub_->async()->Echo(
          &cli_ctx, &request, &response,
          [&cli_ctx, &request, &response, &done, &mu, &cv, val,
           with_binary_metadata](Status s) {
            CHECK(s.ok());

            EXPECT_EQ(request.message(), response.message());
            if (with_binary_metadata) {
              EXPECT_EQ(
                  1u, cli_ctx.GetServerTrailingMetadata().count("custom-bin"));
              EXPECT_EQ(val, ToString(cli_ctx.GetServerTrailingMetadata()
                                          .find("custom-bin")
                                          ->second));
            }
            std::lock_guard<std::mutex> l(mu);
            done = true;
            cv.notify_one();
          });
      std::unique_lock<std::mutex> l(mu);
      while (!done) {
        cv.wait(l);
      }
    }
  }

  void SendRpcsGeneric(int num_rpcs, bool maybe_except,
                       const char* suffix_for_stats) {
    const std::string kMethodName("/grpc.testing.EchoTestService/Echo");
    std::string test_string;
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest request;
      std::unique_ptr<ByteBuffer> send_buf;
      ByteBuffer recv_buf;
      ClientContext cli_ctx;

      test_string += "Hello world. ";
      request.set_message(test_string);
      send_buf = SerializeToByteBuffer(&request);

      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      StubOptions options(suffix_for_stats);
      generic_stub_->UnaryCall(
          &cli_ctx, kMethodName, options, send_buf.get(), &recv_buf,
          [&request, &recv_buf, &done, &mu, &cv, maybe_except](Status s) {
            CHECK(s.ok());

            EchoResponse response;
            EXPECT_TRUE(ParseFromByteBuffer(&recv_buf, &response));
            EXPECT_EQ(request.message(), response.message());
            std::lock_guard<std::mutex> l(mu);
            done = true;
            cv.notify_one();
#if GRPC_ALLOW_EXCEPTIONS
            if (maybe_except) {
              throw -1;
            }
#else
            CHECK(!maybe_except);
#endif
          });
      std::unique_lock<std::mutex> l(mu);
      while (!done) {
        cv.wait(l);
      }
    }
  }

  void SendGenericEchoAsBidi(int num_rpcs, int reuses, bool do_writes_done,
                             const char* suffix_for_stats) {
    const std::string kMethodName("/grpc.testing.EchoTestService/Echo");
    std::string test_string;
    for (int i = 0; i < num_rpcs; i++) {
      test_string += "Hello world. ";
      class Client : public grpc::ClientBidiReactor<ByteBuffer, ByteBuffer> {
       public:
        Client(ClientCallbackEnd2endTest* test, const std::string& method_name,
               const char* suffix_for_stats, const std::string& test_str,
               int reuses, bool do_writes_done)
            : reuses_remaining_(reuses), do_writes_done_(do_writes_done) {
          activate_ = [this, test, method_name, suffix_for_stats, test_str] {
            if (reuses_remaining_ > 0) {
              cli_ctx_ = std::make_unique<ClientContext>();
              reuses_remaining_--;
              StubOptions options(suffix_for_stats);
              test->generic_stub_->PrepareBidiStreamingCall(
                  cli_ctx_.get(), method_name, options, this);
              request_.set_message(test_str);
              send_buf_ = SerializeToByteBuffer(&request_);
              StartWrite(send_buf_.get());
              StartRead(&recv_buf_);
              StartCall();
            } else {
              std::unique_lock<std::mutex> l(mu_);
              done_ = true;
              cv_.notify_one();
            }
          };
          activate_();
        }
        void OnWriteDone(bool /*ok*/) override {
          if (do_writes_done_) {
            StartWritesDone();
          }
        }
        void OnReadDone(bool /*ok*/) override {
          EchoResponse response;
          EXPECT_TRUE(ParseFromByteBuffer(&recv_buf_, &response));
          EXPECT_EQ(request_.message(), response.message());
        };
        void OnDone(const Status& s) override {
          EXPECT_TRUE(s.ok());
          activate_();
        }
        void Await() {
          std::unique_lock<std::mutex> l(mu_);
          while (!done_) {
            cv_.wait(l);
          }
        }

        EchoRequest request_;
        std::unique_ptr<ByteBuffer> send_buf_;
        ByteBuffer recv_buf_;
        std::unique_ptr<ClientContext> cli_ctx_;
        int reuses_remaining_;
        std::function<void()> activate_;
        std::mutex mu_;
        std::condition_variable cv_;
        bool done_ = false;
        const bool do_writes_done_;
      };

      Client rpc(this, kMethodName, suffix_for_stats, test_string, reuses,
                 do_writes_done);

      rpc.Await();
    }
  }
  bool is_server_started_{false};
  int picked_port_{0};
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
  TestServiceImpl service_;
  CallbackTestServiceImpl callback_service_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
};

TEST_P(ClientCallbackEnd2endTest, SimpleRpc) {
  ResetStub();
  SendRpcs(1, false);
}

TEST_P(ClientCallbackEnd2endTest, SimpleRpcExpectedError) {
  ResetStub();

  EchoRequest request;
  EchoResponse response;
  ClientContext cli_ctx;
  ErrorStatus error_status;

  request.set_message("Hello failure");
  error_status.set_code(1);  // CANCELLED
  error_status.set_error_message("cancel error message");
  *request.mutable_param()->mutable_expected_error() = error_status;

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;

  stub_->async()->Echo(&cli_ctx, &request, &response,
                       [&response, &done, &mu, &cv, &error_status](Status s) {
                         EXPECT_EQ("", response.message());
                         EXPECT_EQ(error_status.code(), s.error_code());
                         EXPECT_EQ(error_status.error_message(),
                                   s.error_message());
                         std::lock_guard<std::mutex> l(mu);
                         done = true;
                         cv.notify_one();
                       });

  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

TEST_P(ClientCallbackEnd2endTest, SimpleRpcUnderLockNested) {
  ResetStub();

  // The request/response state associated with an RPC and the synchronization
  // variables needed to notify its completion.
  struct RpcState {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    EchoRequest request;
    EchoResponse response;
    ClientContext cli_ctx;

    RpcState() = default;
    ~RpcState() {
      // Grab the lock to prevent destruction while another is still holding
      // lock
      std::lock_guard<std::mutex> lock(mu);
    }
  };
  std::vector<RpcState> rpc_state(3);
  for (size_t i = 0; i < rpc_state.size(); i++) {
    std::string message = "Hello locked world";
    message += std::to_string(i);
    rpc_state[i].request.set_message(message);
  }

  // Grab a lock and then start an RPC whose callback grabs the same lock and
  // then calls this function to start the next RPC under lock (up to a limit of
  // the size of the rpc_state vector).
  std::function<void(int)> nested_call = [this, &nested_call,
                                          &rpc_state](int index) {
    std::lock_guard<std::mutex> l(rpc_state[index].mu);
    stub_->async()->Echo(&rpc_state[index].cli_ctx, &rpc_state[index].request,
                         &rpc_state[index].response,
                         [index, &nested_call, &rpc_state](Status s) {
                           std::lock_guard<std::mutex> l1(rpc_state[index].mu);
                           EXPECT_TRUE(s.ok());
                           rpc_state[index].done = true;
                           rpc_state[index].cv.notify_all();
                           // Call the next level of nesting if possible
                           if (index + 1 < static_cast<int>(rpc_state.size())) {
                             nested_call(index + 1);
                           }
                         });
  };

  nested_call(0);

  // Wait for completion notifications from all RPCs. Order doesn't matter.
  for (RpcState& state : rpc_state) {
    std::unique_lock<std::mutex> l(state.mu);
    while (!state.done) {
      state.cv.wait(l);
    }
    EXPECT_EQ(state.request.message(), state.response.message());
  }
}

TEST_P(ClientCallbackEnd2endTest, SimpleRpcUnderLock) {
  ResetStub();
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  EchoRequest request;
  request.set_message("Hello locked world.");
  EchoResponse response;
  ClientContext cli_ctx;
  {
    std::lock_guard<std::mutex> l(mu);
    stub_->async()->Echo(&cli_ctx, &request, &response,
                         [&mu, &cv, &done, &request, &response](Status s) {
                           std::lock_guard<std::mutex> l(mu);
                           EXPECT_TRUE(s.ok());
                           EXPECT_EQ(request.message(), response.message());
                           done = true;
                           cv.notify_one();
                         });
  }
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

TEST_P(ClientCallbackEnd2endTest, SequentialRpcs) {
  ResetStub();
  SendRpcs(10, false);
}

TEST_P(ClientCallbackEnd2endTest, SendClientInitialMetadata) {
  ResetStub();
  SimpleRequest request;
  SimpleResponse response;
  ClientContext cli_ctx;

  cli_ctx.AddMetadata(kCheckClientInitialMetadataKey,
                      kCheckClientInitialMetadataVal);

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  stub_->async()->CheckClientInitialMetadata(
      &cli_ctx, &request, &response, [&done, &mu, &cv](Status s) {
        CHECK(s.ok());

        std::lock_guard<std::mutex> l(mu);
        done = true;
        cv.notify_one();
      });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

TEST_P(ClientCallbackEnd2endTest, SimpleRpcWithBinaryMetadata) {
  ResetStub();
  SendRpcs(1, true);
}

TEST_P(ClientCallbackEnd2endTest, SequentialRpcsWithVariedBinaryMetadataValue) {
  ResetStub();
  SendRpcs(10, true);
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcs) {
  ResetStub(std::make_unique<TestInterceptorFactory>(
      "/grpc.testing.EchoTestService/Echo", nullptr));
  SendRpcsGeneric(10, false, /*suffix_for_stats=*/nullptr);
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcsWithSuffix) {
  ResetStub(std::make_unique<TestInterceptorFactory>(
      "/grpc.testing.EchoTestService/Echo", "TestSuffix"));
  SendRpcsGeneric(10, false, "TestSuffix");
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcsAsBidi) {
  ResetStub(std::make_unique<TestInterceptorFactory>(
      "/grpc.testing.EchoTestService/Echo", nullptr));
  SendGenericEchoAsBidi(10, 1, /*do_writes_done=*/true,
                        /*suffix_for_stats=*/nullptr);
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcsAsBidiWithSuffix) {
  ResetStub(std::make_unique<TestInterceptorFactory>(
      "/grpc.testing.EchoTestService/Echo", "TestSuffix"));
  SendGenericEchoAsBidi(10, 1, /*do_writes_done=*/true, "TestSuffix");
}

TEST_P(ClientCallbackEnd2endTest, SequentialGenericRpcsAsBidiWithReactorReuse) {
  ResetStub();
  SendGenericEchoAsBidi(10, 10, /*do_writes_done=*/true,
                        /*suffix_for_stats=*/nullptr);
}

TEST_P(ClientCallbackEnd2endTest, GenericRpcNoWritesDone) {
  ResetStub();
  SendGenericEchoAsBidi(1, 1, /*do_writes_done=*/false,
                        /*suffix_for_stats=*/nullptr);
}

#if GRPC_ALLOW_EXCEPTIONS
TEST_P(ClientCallbackEnd2endTest, ExceptingRpc) {
  ResetStub();
  SendRpcsGeneric(10, true, nullptr);
}
#endif

TEST_P(ClientCallbackEnd2endTest, MultipleRpcsWithVariedBinaryMetadataValue) {
  ResetStub();
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this] { SendRpcs(10, true); });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(ClientCallbackEnd2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([this] { SendRpcs(10, false); });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

TEST_P(ClientCallbackEnd2endTest, CancelRpcBeforeStart) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.TryCancel();

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  stub_->async()->Echo(&context, &request, &response,
                       [&response, &done, &mu, &cv](Status s) {
                         EXPECT_EQ("", response.message());
                         EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
                         std::lock_guard<std::mutex> l(mu);
                         done = true;
                         cv.notify_one();
                       });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, RequestEchoServerCancel) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.AddMetadata(kServerTryCancelRequest,
                      std::to_string(CANCEL_BEFORE_PROCESSING));

  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  stub_->async()->Echo(&context, &request, &response,
                       [&done, &mu, &cv](Status s) {
                         EXPECT_FALSE(s.ok());
                         EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
                         std::lock_guard<std::mutex> l(mu);
                         done = true;
                         cv.notify_one();
                       });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

struct ClientCancelInfo {
  bool cancel{false};
  int ops_before_cancel;

  ClientCancelInfo() : cancel{false} {}
  explicit ClientCancelInfo(int ops) : cancel{true}, ops_before_cancel{ops} {}
};

class WriteClient : public grpc::ClientWriteReactor<EchoRequest> {
 public:
  WriteClient(grpc::testing::EchoTestService::Stub* stub,
              ServerTryCancelRequestPhase server_try_cancel,
              int num_msgs_to_send, ClientCancelInfo client_cancel = {})
      : server_try_cancel_(server_try_cancel),
        num_msgs_to_send_(num_msgs_to_send),
        client_cancel_{client_cancel} {
    std::string msg{"Hello server."};
    for (int i = 0; i < num_msgs_to_send; i++) {
      desired_ += msg;
    }
    if (server_try_cancel != DO_NOT_CANCEL) {
      // Send server_try_cancel value in the client metadata
      context_.AddMetadata(kServerTryCancelRequest,
                           std::to_string(server_try_cancel));
    }
    context_.set_initial_metadata_corked(true);
    stub->async()->RequestStream(&context_, &response_, this);
    StartCall();
    request_.set_message(msg);
    MaybeWrite();
  }
  void OnWriteDone(bool ok) override {
    if (ok) {
      num_msgs_sent_++;
      MaybeWrite();
    }
  }
  void OnDone(const Status& s) override {
    LOG(INFO) << "Sent " << num_msgs_sent_ << " messages";
    int num_to_send =
        (client_cancel_.cancel)
            ? std::min(num_msgs_to_send_, client_cancel_.ops_before_cancel)
            : num_msgs_to_send_;
    switch (server_try_cancel_) {
      case CANCEL_BEFORE_PROCESSING:
      case CANCEL_DURING_PROCESSING:
        // If the RPC is canceled by server before / during messages from the
        // client, it means that the client most likely did not get a chance to
        // send all the messages it wanted to send. i.e num_msgs_sent <=
        // num_msgs_to_send
        EXPECT_LE(num_msgs_sent_, num_to_send);
        break;
      case DO_NOT_CANCEL:
      case CANCEL_AFTER_PROCESSING:
        // If the RPC was not canceled or canceled after all messages were read
        // by the server, the client did get a chance to send all its messages
        EXPECT_EQ(num_msgs_sent_, num_to_send);
        break;
      default:
        assert(false);
        break;
    }
    if ((server_try_cancel_ == DO_NOT_CANCEL) && !client_cancel_.cancel) {
      EXPECT_TRUE(s.ok());
      EXPECT_EQ(response_.message(), desired_);
    } else {
      EXPECT_FALSE(s.ok());
      EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
    }
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_one();
  }
  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  void MaybeWrite() {
    if (client_cancel_.cancel &&
        num_msgs_sent_ == client_cancel_.ops_before_cancel) {
      context_.TryCancel();
    } else if (num_msgs_to_send_ > num_msgs_sent_ + 1) {
      StartWrite(&request_);
    } else if (num_msgs_to_send_ == num_msgs_sent_ + 1) {
      StartWriteLast(&request_, WriteOptions());
    }
  }
  EchoRequest request_;
  EchoResponse response_;
  ClientContext context_;
  const ServerTryCancelRequestPhase server_try_cancel_;
  int num_msgs_sent_{0};
  const int num_msgs_to_send_;
  std::string desired_;
  const ClientCancelInfo client_cancel_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
};

TEST_P(ClientCallbackEnd2endTest, RequestStream) {
  ResetStub();
  WriteClient test{stub_.get(), DO_NOT_CANCEL, 3};
  test.Await();
  // Make sure that the server interceptors were not notified to cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, ClientCancelsRequestStream) {
  ResetStub();
  WriteClient test{stub_.get(), DO_NOT_CANCEL, 3, ClientCancelInfo{2}};
  test.Await();
  // Make sure that the server interceptors got the cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel before doing reading the request
TEST_P(ClientCallbackEnd2endTest, RequestStreamServerCancelBeforeReads) {
  ResetStub();
  WriteClient test{stub_.get(), CANCEL_BEFORE_PROCESSING, 1};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel while reading a request from the stream in parallel
TEST_P(ClientCallbackEnd2endTest, RequestStreamServerCancelDuringRead) {
  ResetStub();
  WriteClient test{stub_.get(), CANCEL_DURING_PROCESSING, 10};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel after reading all the requests but before returning to the
// client
TEST_P(ClientCallbackEnd2endTest, RequestStreamServerCancelAfterReads) {
  ResetStub();
  WriteClient test{stub_.get(), CANCEL_AFTER_PROCESSING, 4};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, UnaryReactor) {
  ResetStub();
  class UnaryClient : public grpc::ClientUnaryReactor {
   public:
    explicit UnaryClient(grpc::testing::EchoTestService::Stub* stub) {
      cli_ctx_.AddMetadata("key1", "val1");
      cli_ctx_.AddMetadata("key2", "val2");
      request_.mutable_param()->set_echo_metadata_initially(true);
      request_.set_message("Hello metadata");
      stub->async()->Echo(&cli_ctx_, &request_, &response_, this);
      StartCall();
    }
    void OnReadInitialMetadataDone(bool ok) override {
      EXPECT_TRUE(ok);
      EXPECT_EQ(1u, cli_ctx_.GetServerInitialMetadata().count("key1"));
      EXPECT_EQ(
          "val1",
          ToString(cli_ctx_.GetServerInitialMetadata().find("key1")->second));
      EXPECT_EQ(1u, cli_ctx_.GetServerInitialMetadata().count("key2"));
      EXPECT_EQ(
          "val2",
          ToString(cli_ctx_.GetServerInitialMetadata().find("key2")->second));
      initial_metadata_done_ = true;
    }
    void OnDone(const Status& s) override {
      EXPECT_TRUE(initial_metadata_done_);
      EXPECT_EQ(0u, cli_ctx_.GetServerTrailingMetadata().size());
      EXPECT_TRUE(s.ok());
      EXPECT_EQ(request_.message(), response_.message());
      std::unique_lock<std::mutex> l(mu_);
      done_ = true;
      cv_.notify_one();
    }
    void Await() {
      std::unique_lock<std::mutex> l(mu_);
      while (!done_) {
        cv_.wait(l);
      }
    }

   private:
    EchoRequest request_;
    EchoResponse response_;
    ClientContext cli_ctx_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_{false};
    bool initial_metadata_done_{false};
  };

  UnaryClient test{stub_.get()};
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, GenericUnaryReactor) {
  const std::string kMethodName("/grpc.testing.EchoTestService/Echo");
  constexpr char kSuffixForStats[] = "TestSuffixForStats";
  ResetStub(
      std::make_unique<TestInterceptorFactory>(kMethodName, kSuffixForStats));
  class UnaryClient : public grpc::ClientUnaryReactor {
   public:
    UnaryClient(grpc::GenericStub* stub, const std::string& method_name,
                const char* suffix_for_stats) {
      cli_ctx_.AddMetadata("key1", "val1");
      cli_ctx_.AddMetadata("key2", "val2");
      request_.mutable_param()->set_echo_metadata_initially(true);
      request_.set_message("Hello metadata");
      send_buf_ = SerializeToByteBuffer(&request_);

      StubOptions options(suffix_for_stats);
      stub->PrepareUnaryCall(&cli_ctx_, method_name, options, send_buf_.get(),
                             &recv_buf_, this);
      StartCall();
    }
    void OnReadInitialMetadataDone(bool ok) override {
      EXPECT_TRUE(ok);
      EXPECT_EQ(1u, cli_ctx_.GetServerInitialMetadata().count("key1"));
      EXPECT_EQ(
          "val1",
          ToString(cli_ctx_.GetServerInitialMetadata().find("key1")->second));
      EXPECT_EQ(1u, cli_ctx_.GetServerInitialMetadata().count("key2"));
      EXPECT_EQ(
          "val2",
          ToString(cli_ctx_.GetServerInitialMetadata().find("key2")->second));
      initial_metadata_done_ = true;
    }
    void OnDone(const Status& s) override {
      EXPECT_TRUE(initial_metadata_done_);
      EXPECT_EQ(0u, cli_ctx_.GetServerTrailingMetadata().size());
      EXPECT_TRUE(s.ok());
      EchoResponse response;
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buf_, &response));
      EXPECT_EQ(request_.message(), response.message());
      std::unique_lock<std::mutex> l(mu_);
      done_ = true;
      cv_.notify_one();
    }
    void Await() {
      std::unique_lock<std::mutex> l(mu_);
      while (!done_) {
        cv_.wait(l);
      }
    }

   private:
    EchoRequest request_;
    std::unique_ptr<ByteBuffer> send_buf_;
    ByteBuffer recv_buf_;
    ClientContext cli_ctx_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_{false};
    bool initial_metadata_done_{false};
  };

  UnaryClient test{generic_stub_.get(), kMethodName, kSuffixForStats};
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

class ReadClient : public grpc::ClientReadReactor<EchoResponse> {
 public:
  ReadClient(grpc::testing::EchoTestService::Stub* stub,
             ServerTryCancelRequestPhase server_try_cancel,
             ClientCancelInfo client_cancel = {})
      : server_try_cancel_(server_try_cancel), client_cancel_{client_cancel} {
    if (server_try_cancel_ != DO_NOT_CANCEL) {
      // Send server_try_cancel value in the client metadata
      context_.AddMetadata(kServerTryCancelRequest,
                           std::to_string(server_try_cancel));
    }
    request_.set_message("Hello client ");
    stub->async()->ResponseStream(&context_, &request_, this);
    if (client_cancel_.cancel &&
        reads_complete_ == client_cancel_.ops_before_cancel) {
      context_.TryCancel();
    }
    // Even if we cancel, read until failure because there might be responses
    // pending
    StartRead(&response_);
    StartCall();
  }
  void OnReadDone(bool ok) override {
    if (!ok) {
      if (server_try_cancel_ == DO_NOT_CANCEL && !client_cancel_.cancel) {
        EXPECT_EQ(reads_complete_, kServerDefaultResponseStreamsToSend);
      }
    } else {
      EXPECT_LE(reads_complete_, kServerDefaultResponseStreamsToSend);
      EXPECT_EQ(response_.message(),
                request_.message() + std::to_string(reads_complete_));
      reads_complete_++;
      if (client_cancel_.cancel &&
          reads_complete_ == client_cancel_.ops_before_cancel) {
        context_.TryCancel();
      }
      // Even if we cancel, read until failure because there might be responses
      // pending
      StartRead(&response_);
    }
  }
  void OnDone(const Status& s) override {
    LOG(INFO) << "Read " << reads_complete_ << " messages";
    switch (server_try_cancel_) {
      case DO_NOT_CANCEL:
        if (!client_cancel_.cancel || client_cancel_.ops_before_cancel >
                                          kServerDefaultResponseStreamsToSend) {
          EXPECT_TRUE(s.ok());
          EXPECT_EQ(reads_complete_, kServerDefaultResponseStreamsToSend);
        } else {
          EXPECT_GE(reads_complete_, client_cancel_.ops_before_cancel);
          EXPECT_LE(reads_complete_, kServerDefaultResponseStreamsToSend);
          // Status might be ok or cancelled depending on whether server
          // sent status before client cancel went through
          if (!s.ok()) {
            EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
          }
        }
        break;
      case CANCEL_BEFORE_PROCESSING:
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        EXPECT_EQ(reads_complete_, 0);
        break;
      case CANCEL_DURING_PROCESSING:
      case CANCEL_AFTER_PROCESSING:
        // If server canceled while writing messages, client must have read
        // less than or equal to the expected number of messages. Even if the
        // server canceled after writing all messages, the RPC may be canceled
        // before the Client got a chance to read all the messages.
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        EXPECT_LE(reads_complete_, kServerDefaultResponseStreamsToSend);
        break;
      default:
        assert(false);
    }
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_one();
  }
  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  EchoRequest request_;
  EchoResponse response_;
  ClientContext context_;
  const ServerTryCancelRequestPhase server_try_cancel_;
  int reads_complete_{0};
  const ClientCancelInfo client_cancel_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
};

TEST_P(ClientCallbackEnd2endTest, ResponseStream) {
  ResetStub();
  ReadClient test{stub_.get(), DO_NOT_CANCEL};
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, ClientCancelsResponseStream) {
  ResetStub();
  ReadClient test{stub_.get(), DO_NOT_CANCEL, ClientCancelInfo{2}};
  test.Await();
  // Because cancel in this case races with server finish, we can't be sure that
  // server interceptors even see cancellation
}

// Server to cancel before sending any response messages
TEST_P(ClientCallbackEnd2endTest, ResponseStreamServerCancelBefore) {
  ResetStub();
  ReadClient test{stub_.get(), CANCEL_BEFORE_PROCESSING};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel while writing a response to the stream in parallel
TEST_P(ClientCallbackEnd2endTest, ResponseStreamServerCancelDuring) {
  ResetStub();
  ReadClient test{stub_.get(), CANCEL_DURING_PROCESSING};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel after writing all the responses to the stream but before
// returning to the client
TEST_P(ClientCallbackEnd2endTest, ResponseStreamServerCancelAfter) {
  ResetStub();
  ReadClient test{stub_.get(), CANCEL_AFTER_PROCESSING};
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

class BidiClient : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  BidiClient(grpc::testing::EchoTestService::Stub* stub,
             ServerTryCancelRequestPhase server_try_cancel,
             int num_msgs_to_send, bool cork_metadata, bool first_write_async,
             ClientCancelInfo client_cancel = {})
      : server_try_cancel_(server_try_cancel),
        msgs_to_send_{num_msgs_to_send},
        client_cancel_{client_cancel} {
    if (server_try_cancel_ != DO_NOT_CANCEL) {
      // Send server_try_cancel value in the client metadata
      context_.AddMetadata(kServerTryCancelRequest,
                           std::to_string(server_try_cancel));
    }
    request_.set_message("Hello fren ");
    context_.set_initial_metadata_corked(cork_metadata);
    stub->async()->BidiStream(&context_, this);
    MaybeAsyncWrite(first_write_async);
    StartRead(&response_);
    StartCall();
  }
  void OnReadDone(bool ok) override {
    if (!ok) {
      if (server_try_cancel_ == DO_NOT_CANCEL) {
        if (!client_cancel_.cancel) {
          EXPECT_EQ(reads_complete_, msgs_to_send_);
        } else {
          EXPECT_LE(reads_complete_, writes_complete_);
        }
      }
    } else {
      EXPECT_LE(reads_complete_, msgs_to_send_);
      EXPECT_EQ(response_.message(), request_.message());
      reads_complete_++;
      StartRead(&response_);
    }
  }
  void OnWriteDone(bool ok) override {
    if (async_write_thread_.joinable()) {
      async_write_thread_.join();
      RemoveHold();
    }
    if (server_try_cancel_ == DO_NOT_CANCEL) {
      EXPECT_TRUE(ok);
    } else if (!ok) {
      return;
    }
    writes_complete_++;
    MaybeWrite();
  }
  void OnDone(const Status& s) override {
    LOG(INFO) << "Sent " << writes_complete_ << " messages";
    LOG(INFO) << "Read " << reads_complete_ << " messages";
    switch (server_try_cancel_) {
      case DO_NOT_CANCEL:
        if (!client_cancel_.cancel ||
            client_cancel_.ops_before_cancel > msgs_to_send_) {
          EXPECT_TRUE(s.ok());
          EXPECT_EQ(writes_complete_, msgs_to_send_);
          EXPECT_EQ(reads_complete_, writes_complete_);
        } else {
          EXPECT_FALSE(s.ok());
          EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
          EXPECT_EQ(writes_complete_, client_cancel_.ops_before_cancel);
          EXPECT_LE(reads_complete_, writes_complete_);
        }
        break;
      case CANCEL_BEFORE_PROCESSING:
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        // The RPC is canceled before the server did any work or returned any
        // reads, but it's possible that some writes took place first from the
        // client
        EXPECT_LE(writes_complete_, msgs_to_send_);
        EXPECT_EQ(reads_complete_, 0);
        break;
      case CANCEL_DURING_PROCESSING:
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        EXPECT_LE(writes_complete_, msgs_to_send_);
        EXPECT_LE(reads_complete_, writes_complete_);
        break;
      case CANCEL_AFTER_PROCESSING:
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(grpc::StatusCode::CANCELLED, s.error_code());
        EXPECT_EQ(writes_complete_, msgs_to_send_);
        // The Server canceled after reading the last message and after writing
        // the message to the client. However, the RPC cancellation might have
        // taken effect before the client actually read the response.
        EXPECT_LE(reads_complete_, writes_complete_);
        break;
      default:
        assert(false);
    }
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_one();
  }
  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  void MaybeAsyncWrite(bool first_write_async) {
    if (first_write_async) {
      // Make sure that we have a write to issue.
      // TODO(vjpai): Make this work with 0 writes case as well.
      assert(msgs_to_send_ >= 1);

      AddHold();
      async_write_thread_ = std::thread([this] {
        std::unique_lock<std::mutex> lock(async_write_thread_mu_);
        async_write_thread_cv_.wait(
            lock, [this] { return async_write_thread_start_; });
        MaybeWrite();
      });
      std::lock_guard<std::mutex> lock(async_write_thread_mu_);
      async_write_thread_start_ = true;
      async_write_thread_cv_.notify_one();
      return;
    }
    MaybeWrite();
  }
  void MaybeWrite() {
    if (client_cancel_.cancel &&
        writes_complete_ == client_cancel_.ops_before_cancel) {
      context_.TryCancel();
    } else if (writes_complete_ == msgs_to_send_) {
      StartWritesDone();
    } else {
      StartWrite(&request_);
    }
  }
  EchoRequest request_;
  EchoResponse response_;
  ClientContext context_;
  const ServerTryCancelRequestPhase server_try_cancel_;
  int reads_complete_{0};
  int writes_complete_{0};
  const int msgs_to_send_;
  const ClientCancelInfo client_cancel_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
  std::thread async_write_thread_;
  bool async_write_thread_start_ = false;
  std::mutex async_write_thread_mu_;
  std::condition_variable async_write_thread_cv_;
};

TEST_P(ClientCallbackEnd2endTest, BidiStream) {
  ResetStub();
  BidiClient test(stub_.get(), DO_NOT_CANCEL,
                  kServerDefaultResponseStreamsToSend,
                  /*cork_metadata=*/false, /*first_write_async=*/false);
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, BidiStreamFirstWriteAsync) {
  ResetStub();
  BidiClient test(stub_.get(), DO_NOT_CANCEL,
                  kServerDefaultResponseStreamsToSend,
                  /*cork_metadata=*/false, /*first_write_async=*/true);
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, BidiStreamCorked) {
  ResetStub();
  BidiClient test(stub_.get(), DO_NOT_CANCEL,
                  kServerDefaultResponseStreamsToSend,
                  /*cork_metadata=*/true, /*first_write_async=*/false);
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, BidiStreamCorkedFirstWriteAsync) {
  ResetStub();
  BidiClient test(stub_.get(), DO_NOT_CANCEL,
                  kServerDefaultResponseStreamsToSend,
                  /*cork_metadata=*/true, /*first_write_async=*/true);
  test.Await();
  // Make sure that the server interceptors were not notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(0, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, ClientCancelsBidiStream) {
  ResetStub();
  BidiClient test(stub_.get(), DO_NOT_CANCEL,
                  kServerDefaultResponseStreamsToSend,
                  /*cork_metadata=*/false, /*first_write_async=*/false,
                  ClientCancelInfo(2));
  test.Await();
  // Make sure that the server interceptors were notified of a cancel
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel before reading/writing any requests/responses on the stream
TEST_P(ClientCallbackEnd2endTest, BidiStreamServerCancelBefore) {
  ResetStub();
  BidiClient test(stub_.get(), CANCEL_BEFORE_PROCESSING, /*num_msgs_to_send=*/2,
                  /*cork_metadata=*/false, /*first_write_async=*/false);
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel while reading/writing requests/responses on the stream in
// parallel
TEST_P(ClientCallbackEnd2endTest, BidiStreamServerCancelDuring) {
  ResetStub();
  BidiClient test(stub_.get(), CANCEL_DURING_PROCESSING,
                  /*num_msgs_to_send=*/10, /*cork_metadata=*/false,
                  /*first_write_async=*/false);
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

// Server to cancel after reading/writing all requests/responses on the stream
// but before returning to the client
TEST_P(ClientCallbackEnd2endTest, BidiStreamServerCancelAfter) {
  ResetStub();
  BidiClient test(stub_.get(), CANCEL_AFTER_PROCESSING, /*num_msgs_to_send=*/5,
                  /*cork_metadata=*/false, /*first_write_async=*/false);
  test.Await();
  // Make sure that the server interceptors were notified
  if (GetParam().use_interceptors) {
    EXPECT_EQ(20, PhonyInterceptor::GetNumTimesCancel());
  }
}

TEST_P(ClientCallbackEnd2endTest, SimultaneousReadAndWritesDone) {
  ResetStub();
  class Client : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
   public:
    explicit Client(grpc::testing::EchoTestService::Stub* stub) {
      request_.set_message("Hello bidi ");
      stub->async()->BidiStream(&context_, this);
      StartWrite(&request_);
      StartCall();
    }
    void OnReadDone(bool ok) override {
      EXPECT_TRUE(ok);
      EXPECT_EQ(response_.message(), request_.message());
    }
    void OnWriteDone(bool ok) override {
      EXPECT_TRUE(ok);
      // Now send out the simultaneous Read and WritesDone
      StartWritesDone();
      StartRead(&response_);
    }
    void OnDone(const Status& s) override {
      EXPECT_TRUE(s.ok());
      EXPECT_EQ(response_.message(), request_.message());
      std::unique_lock<std::mutex> l(mu_);
      done_ = true;
      cv_.notify_one();
    }
    void Await() {
      std::unique_lock<std::mutex> l(mu_);
      while (!done_) {
        cv_.wait(l);
      }
    }

   private:
    EchoRequest request_;
    EchoResponse response_;
    ClientContext context_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
  } test{stub_.get()};

  test.Await();
}

TEST_P(ClientCallbackEnd2endTest, UnimplementedRpc) {
  ChannelArguments args;
  const auto& channel_creds = GetCredentialsProvider()->GetChannelCredentials(
      GetParam().credentials_type, &args);
  std::shared_ptr<Channel> channel =
      (GetParam().protocol == Protocol::TCP)
          ? grpc::CreateCustomChannel(server_address_.str(), channel_creds,
                                      args)
          : server_->InProcessChannel(args);
  std::unique_ptr<grpc::testing::UnimplementedEchoService::Stub> stub;
  stub = grpc::testing::UnimplementedEchoService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  ClientContext cli_ctx;
  request.set_message("Hello world.");
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  stub->async()->Unimplemented(
      &cli_ctx, &request, &response, [&done, &mu, &cv](Status s) {
        EXPECT_EQ(StatusCode::UNIMPLEMENTED, s.error_code());
        EXPECT_EQ("", s.error_message());

        std::lock_guard<std::mutex> l(mu);
        done = true;
        cv.notify_one();
      });
  std::unique_lock<std::mutex> l(mu);
  while (!done) {
    cv.wait(l);
  }
}

TEST_P(ClientCallbackEnd2endTest, TestTrailersOnlyOnError) {
  // Note that trailers-only is an HTTP/2 concept so we shouldn't do this test
  // for any other transport such as inproc.
  if (GetParam().protocol != Protocol::TCP) {
    return;
  }

  ResetStub();
  class Reactor : public grpc::ClientBidiReactor<EchoRequest, EchoResponse> {
   public:
    explicit Reactor(grpc::testing::EchoTestService::Stub* stub) {
      stub->async()->UnimplementedBidi(&context_, this);
      StartCall();
    }
    void Await() {
      std::unique_lock<std::mutex> l(mu_);
      while (!done_) {
        done_cv_.wait(l);
      }
    }

   private:
    void OnReadInitialMetadataDone(bool ok) override { EXPECT_FALSE(ok); }
    void OnDone(const Status& s) override {
      EXPECT_EQ(s.error_code(), grpc::StatusCode::UNIMPLEMENTED);
      EXPECT_EQ(s.error_message(), "");
      std::unique_lock<std::mutex> l(mu_);
      done_ = true;
      done_cv_.notify_one();
    }

    ClientContext context_;
    std::mutex mu_;
    std::condition_variable done_cv_;
    bool done_ = false;
  } client(stub_.get());

  client.Await();
}

TEST_P(ClientCallbackEnd2endTest,
       ResponseStreamExtraReactionFlowReadsUntilDone) {
  ResetStub();
  class ReadAllIncomingDataClient
      : public grpc::ClientReadReactor<EchoResponse> {
   public:
    explicit ReadAllIncomingDataClient(
        grpc::testing::EchoTestService::Stub* stub) {
      request_.set_message("Hello client ");
      stub->async()->ResponseStream(&context_, &request_, this);
    }
    bool WaitForReadDone() {
      std::unique_lock<std::mutex> l(mu_);
      while (!read_done_) {
        read_cv_.wait(l);
      }
      read_done_ = false;
      return read_ok_;
    }
    void Await() {
      std::unique_lock<std::mutex> l(mu_);
      while (!done_) {
        done_cv_.wait(l);
      }
    }
    // RemoveHold under the same lock used for OnDone to make sure that we don't
    // call OnDone directly or indirectly from the RemoveHold function.
    void RemoveHoldUnderLock() {
      std::unique_lock<std::mutex> l(mu_);
      RemoveHold();
    }
    const Status& status() {
      std::unique_lock<std::mutex> l(mu_);
      return status_;
    }

   private:
    void OnReadDone(bool ok) override {
      std::unique_lock<std::mutex> l(mu_);
      read_ok_ = ok;
      read_done_ = true;
      read_cv_.notify_one();
    }
    void OnDone(const Status& s) override {
      std::unique_lock<std::mutex> l(mu_);
      done_ = true;
      status_ = s;
      done_cv_.notify_one();
    }

    EchoRequest request_;
    EchoResponse response_;
    ClientContext context_;
    bool read_ok_ = false;
    bool read_done_ = false;
    std::mutex mu_;
    std::condition_variable read_cv_;
    std::condition_variable done_cv_;
    bool done_ = false;
    Status status_;
  } client{stub_.get()};

  int reads_complete = 0;
  client.AddHold();
  client.StartCall();

  EchoResponse response;
  bool read_ok = true;
  while (read_ok) {
    client.StartRead(&response);
    read_ok = client.WaitForReadDone();
    if (read_ok) {
      ++reads_complete;
    }
  }
  client.RemoveHoldUnderLock();
  client.Await();

  EXPECT_EQ(kServerDefaultResponseStreamsToSend, reads_complete);
  EXPECT_EQ(client.status().error_code(), grpc::StatusCode::OK);
}

std::vector<TestScenario> CreateTestScenarios(bool test_insecure) {
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif

  std::vector<TestScenario> scenarios;
  std::vector<std::string> credentials_types{
      GetCredentialsProvider()->GetSecureCredentialsTypeList()};
  auto insec_ok = [] {
    // Only allow insecure credentials type when it is registered with the
    // provider. User may create providers that do not have insecure.
    return GetCredentialsProvider()->GetChannelCredentials(
               kInsecureCredentialsType, nullptr) != nullptr;
  };
  if (test_insecure && insec_ok()) {
    credentials_types.push_back(kInsecureCredentialsType);
  }
  CHECK(!credentials_types.empty());

  bool barr[]{false, true};
  Protocol parr[]{Protocol::INPROC, Protocol::TCP};
  for (Protocol p : parr) {
    for (const auto& cred : credentials_types) {
      // TODO(vjpai): Test inproc with secure credentials when feasible
      if (p == Protocol::INPROC &&
          (cred != kInsecureCredentialsType || !insec_ok())) {
        continue;
      }
      for (bool callback_server : barr) {
        for (bool use_interceptors : barr) {
          scenarios.emplace_back(callback_server, p, use_interceptors, cred);
        }
      }
    }
  }
  return scenarios;
}

INSTANTIATE_TEST_SUITE_P(ClientCallbackEnd2endTest, ClientCallbackEnd2endTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
