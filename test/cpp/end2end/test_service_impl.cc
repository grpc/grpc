//
//
// Copyright 2016 gRPC authors.
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

#include "test/cpp/end2end/test_service_impl.h"

#include <grpcpp/alarm.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_context.h>

#include <string>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/util/crash.h"
#include "src/core/util/notification.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/util/string_ref_helper.h"

using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace internal {

// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContextBase* context, const EchoRequest* request,
                       EchoResponse* response) {
  if (request->has_param() && request->param().echo_deadline()) {
    gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    if (context->deadline() != system_clock::time_point::max()) {
      Timepoint2Timespec(context->deadline(), &deadline);
    }
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}

void CheckServerAuthContext(const ServerContextBase* context,
                            const std::string& expected_transport_security_type,
                            const std::string& expected_client_identity) {
  std::shared_ptr<const AuthContext> auth_ctx = context->auth_context();
  std::vector<grpc::string_ref> tst =
      auth_ctx->FindPropertyValues("transport_security_type");
  EXPECT_EQ(1u, tst.size());
  EXPECT_EQ(expected_transport_security_type, ToString(tst[0]));
  if (expected_client_identity.empty()) {
    EXPECT_TRUE(auth_ctx->GetPeerIdentityPropertyName().empty());
    EXPECT_TRUE(auth_ctx->GetPeerIdentity().empty());
    EXPECT_FALSE(auth_ctx->IsPeerAuthenticated());
  } else {
    auto identity = auth_ctx->GetPeerIdentity();
    EXPECT_TRUE(auth_ctx->IsPeerAuthenticated());
    EXPECT_EQ(1u, identity.size());
    EXPECT_EQ(expected_client_identity, identity[0]);
  }
}

// Returns the number of pairs in metadata that exactly match the given
// key-value pair. Returns -1 if the pair wasn't found.
int MetadataMatchCount(
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    const std::string& key, const std::string& value) {
  int count = 0;
  for (const auto& [k, v] : metadata) {
    if (ToString(k) == key && ToString(v) == value) {
      ++count;
    }
  }
  return count;
}

int GetIntValueFromMetadataHelper(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
    LOG(INFO) << key << " : " << default_value;
  }

  return default_value;
}

int GetIntValueFromMetadata(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  return GetIntValueFromMetadataHelper(key, metadata, default_value);
}

void ServerTryCancel(ServerContext* context) {
  EXPECT_FALSE(context->IsCancelled());
  context->TryCancel();
  LOG(INFO) << "Server called TryCancel() to cancel the request";
  // Now wait until it's really canceled
  while (!context->IsCancelled()) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1000, GPR_TIMESPAN)));
  }
}

void ServerTryCancelNonblocking(CallbackServerContext* context) {
  EXPECT_FALSE(context->IsCancelled());
  context->TryCancel();
  LOG(INFO) << "Server called TryCancelNonblocking() to cancel the request";
}

}  // namespace internal

ServerUnaryReactor* CallbackTestServiceImpl::Echo(
    CallbackServerContext* context, const EchoRequest* request,
    EchoResponse* response) {
  class Reactor : public grpc::ServerUnaryReactor {
   public:
    Reactor(CallbackTestServiceImpl* service, CallbackServerContext* ctx,
            const EchoRequest* request, EchoResponse* response)
        : service_(service), ctx_(ctx), req_(request), resp_(response) {
      // It should be safe to call IsCancelled here, even though we don't know
      // the result. Call it asynchronously to see if we trigger any data races.
      // Join it in OnDone (technically that could be blocking but shouldn't be
      // for very long).
      async_cancel_check_ = std::thread([this] { (void)ctx_->IsCancelled(); });

      started_ = true;

      if (request->has_param() &&
          request->param().server_notify_client_when_started()) {
        service->signaller_.SignalClientThatRpcStarted();
        // Block on the "wait to continue" decision in a different thread since
        // we can't tie up an EM thread with blocking events. We can join it in
        // OnDone since it would definitely be done by then.
        rpc_wait_thread_ = std::thread([this] {
          service_->signaller_.ServerWaitToContinue();
          StartRpc();
        });
      } else {
        StartRpc();
      }
    }

    void StartRpc() {
      if (req_->has_param() && req_->param().server_sleep_us() > 0) {
        // Set an alarm for that much time
        alarm_.Set(
            gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                         gpr_time_from_micros(req_->param().server_sleep_us() *
                                                  grpc_test_slowdown_factor(),
                                              GPR_TIMESPAN)),
            [this](bool ok) { NonDelayed(ok); });
        return;
      }
      NonDelayed(true);
    }
    void OnSendInitialMetadataDone(bool ok) override {
      EXPECT_TRUE(ok);
      initial_metadata_sent_ = true;
    }
    void OnCancel() override {
      EXPECT_TRUE(started_);
      EXPECT_TRUE(ctx_->IsCancelled());
      on_cancel_invoked_ = true;
      std::lock_guard<std::mutex> l(cancel_mu_);
      cancel_cv_.notify_one();
    }
    void OnDone() override {
      if (req_->has_param() && req_->param().echo_metadata_initially()) {
        EXPECT_TRUE(initial_metadata_sent_);
      }
      EXPECT_EQ(ctx_->IsCancelled(), on_cancel_invoked_);
      // Validate that finishing with a non-OK status doesn't cause cancellation
      if (req_->has_param() && req_->param().has_expected_error()) {
        EXPECT_FALSE(on_cancel_invoked_);
      }
      async_cancel_check_.join();
      if (rpc_wait_thread_.joinable()) {
        rpc_wait_thread_.join();
      }
      if (finish_when_cancelled_.joinable()) {
        finish_when_cancelled_.join();
      }
      delete this;
    }

   private:
    void NonDelayed(bool ok) {
      if (!ok) {
        EXPECT_TRUE(ctx_->IsCancelled());
        Finish(Status::CANCELLED);
        return;
      }
      if (req_->has_param() && req_->param().server_die()) {
        LOG(ERROR) << "The request should not reach application handler.";
        CHECK(0);
      }
      if (req_->has_param() && req_->param().has_expected_error()) {
        const auto& error = req_->param().expected_error();
        Finish(Status(static_cast<StatusCode>(error.code()),
                      error.error_message(), error.binary_error_details()));
        return;
      }
      int server_try_cancel = internal::GetIntValueFromMetadata(
          kServerTryCancelRequest, ctx_->client_metadata(), DO_NOT_CANCEL);
      if (server_try_cancel != DO_NOT_CANCEL) {
        // Since this is a unary RPC, by the time this server handler is called,
        // the 'request' message is already read from the client. So the
        // scenarios in server_try_cancel don't make much sense. Just cancel the
        // RPC as long as server_try_cancel is not DO_NOT_CANCEL
        EXPECT_FALSE(ctx_->IsCancelled());
        ctx_->TryCancel();
        LOG(INFO) << "Server called TryCancel() to cancel the request";
        FinishWhenCancelledAsync();
        return;
      }
      if (req_->has_param() &&
          req_->param().compression_algorithm() != RequestParams::NONE) {
        if (req_->param().compression_algorithm() == RequestParams::DEFLATE) {
          ctx_->set_compression_algorithm(GRPC_COMPRESS_DEFLATE);
        } else if (req_->param().compression_algorithm() ==
                   RequestParams::GZIP) {
          ctx_->set_compression_algorithm(GRPC_COMPRESS_GZIP);
        }
      }
      resp_->set_message(req_->message());
      internal::MaybeEchoDeadline(ctx_, req_, resp_);
      if (service_->host_) {
        resp_->mutable_param()->set_host(*service_->host_);
      } else if (req_->has_param() &&
                 req_->param().echo_host_from_authority_header()) {
        auto authority = ctx_->ExperimentalGetAuthority();
        std::string authority_str(authority.data(), authority.size());
        resp_->mutable_param()->set_host(std::move(authority_str));
      }
      if (req_->has_param() && req_->param().client_cancel_after_us()) {
        {
          std::unique_lock<std::mutex> lock(service_->mu_);
          service_->signal_client_ = true;
        }
        FinishWhenCancelledAsync();
        return;
      } else if (req_->has_param() && req_->param().server_cancel_after_us()) {
        alarm_.Set(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                gpr_time_from_micros(
                                    req_->param().server_cancel_after_us() *
                                        grpc_test_slowdown_factor(),
                                    GPR_TIMESPAN)),
                   [this](bool) { Finish(Status::CANCELLED); });
        return;
      } else if (!req_->has_param() || !req_->param().skip_cancelled_check()) {
        EXPECT_FALSE(ctx_->IsCancelled());
      }

      if (req_->has_param() && req_->param().echo_metadata_initially()) {
        const std::multimap<grpc::string_ref, grpc::string_ref>&
            client_metadata = ctx_->client_metadata();
        for (const auto& [key, value] : client_metadata) {
          ctx_->AddInitialMetadata(ToString(key), ToString(value));
        }
        StartSendInitialMetadata();
      }

      if (req_->has_param() && req_->param().echo_metadata()) {
        const std::multimap<grpc::string_ref, grpc::string_ref>&
            client_metadata = ctx_->client_metadata();
        for (const auto& [key, value] : client_metadata) {
          ctx_->AddTrailingMetadata(ToString(key), ToString(value));
        }
        // Terminate rpc with error and debug info in trailer.
        if (req_->param().debug_info().stack_entries_size() ||
            !req_->param().debug_info().detail().empty()) {
          std::string serialized_debug_info =
              req_->param().debug_info().SerializeAsString();
          ctx_->AddTrailingMetadata(kDebugInfoTrailerKey,
                                    serialized_debug_info);
          Finish(Status::CANCELLED);
          return;
        }
      }
      if (req_->has_param() &&
          (!req_->param().expected_client_identity().empty() ||
           req_->param().check_auth_context())) {
        internal::CheckServerAuthContext(
            ctx_, req_->param().expected_transport_security_type(),
            req_->param().expected_client_identity());
      }
      if (req_->has_param() && req_->param().response_message_length() > 0) {
        resp_->set_message(
            std::string(req_->param().response_message_length(), '\0'));
      }
      if (req_->has_param() && req_->param().echo_peer()) {
        resp_->mutable_param()->set_peer(ctx_->peer());
      }
      Finish(Status::OK);
    }
    void FinishWhenCancelledAsync() {
      finish_when_cancelled_ = std::thread([this] {
        std::unique_lock<std::mutex> l(cancel_mu_);
        cancel_cv_.wait(l, [this] { return ctx_->IsCancelled(); });
        Finish(Status::CANCELLED);
      });
    }

    CallbackTestServiceImpl* const service_;
    CallbackServerContext* const ctx_;
    const EchoRequest* const req_;
    EchoResponse* const resp_;
    Alarm alarm_;
    std::mutex cancel_mu_;
    std::condition_variable cancel_cv_;
    bool initial_metadata_sent_ = false;
    bool started_ = false;
    bool on_cancel_invoked_ = false;
    std::thread async_cancel_check_;
    std::thread rpc_wait_thread_;
    std::thread finish_when_cancelled_;
  };

  return new Reactor(this, context, request, response);
}

ServerUnaryReactor* CallbackTestServiceImpl::CheckClientInitialMetadata(
    CallbackServerContext* context, const SimpleRequest*, SimpleResponse*) {
  class Reactor : public grpc::ServerUnaryReactor {
   public:
    explicit Reactor(CallbackServerContext* ctx) {
      EXPECT_EQ(internal::MetadataMatchCount(ctx->client_metadata(),
                                             kCheckClientInitialMetadataKey,
                                             kCheckClientInitialMetadataVal),
                1);
      EXPECT_EQ(ctx->client_metadata().count(kCheckClientInitialMetadataKey),
                1u);
      Finish(Status::OK);
    }
    void OnDone() override { delete this; }
  };

  return new Reactor(context);
}

ServerReadReactor<EchoRequest>* CallbackTestServiceImpl::RequestStream(
    CallbackServerContext* context, EchoResponse* response) {
  // If 'server_try_cancel' is set in the metadata, the RPC is cancelled by
  // the server by calling ServerContext::TryCancel() depending on the
  // value:
  //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server
  //   reads any message from the client CANCEL_DURING_PROCESSING: The RPC
  //   is cancelled while the server is reading messages from the client
  //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server reads
  //   all the messages from the client
  int server_try_cancel = internal::GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);
  if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
    internal::ServerTryCancelNonblocking(context);
    // Don't need to provide a reactor since the RPC is canceled
    return nullptr;
  }

  class Reactor : public grpc::ServerReadReactor<EchoRequest> {
   public:
    Reactor(CallbackServerContext* ctx, EchoResponse* response,
            int server_try_cancel)
        : ctx_(ctx),
          response_(response),
          server_try_cancel_(server_try_cancel) {
      EXPECT_NE(server_try_cancel, CANCEL_BEFORE_PROCESSING);
      response->set_message("");

      if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
        ctx->TryCancel();
        // Don't wait for it here
      }
      StartRead(&request_);
      setup_done_ = true;
    }
    void OnDone() override { delete this; }
    void OnCancel() override {
      EXPECT_TRUE(setup_done_);
      EXPECT_TRUE(ctx_->IsCancelled());
      FinishOnce(Status::CANCELLED);
    }
    void OnReadDone(bool ok) override {
      if (ok) {
        response_->mutable_message()->append(request_.message());
        num_msgs_read_++;
        StartRead(&request_);
      } else {
        LOG(INFO) << "Read: " << num_msgs_read_ << " messages";

        if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
          // Let OnCancel recover this
          return;
        }
        if (server_try_cancel_ == CANCEL_AFTER_PROCESSING) {
          internal::ServerTryCancelNonblocking(ctx_);
          return;
        }
        FinishOnce(Status::OK);
      }
    }

   private:
    void FinishOnce(const Status& s) {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        Finish(s);
        finished_ = true;
      }
    }

    CallbackServerContext* const ctx_;
    EchoResponse* const response_;
    EchoRequest request_;
    int num_msgs_read_{0};
    int server_try_cancel_;
    std::mutex finish_mu_;
    bool finished_{false};
    bool setup_done_{false};
  };

  return new Reactor(context, response, server_try_cancel);
}

// Return 'kNumResponseStreamMsgs' messages.
// TODO(yangg) make it generic by adding a parameter into EchoRequest
ServerWriteReactor<EchoResponse>* CallbackTestServiceImpl::ResponseStream(
    CallbackServerContext* context, const EchoRequest* request) {
  // If 'server_try_cancel' is set in the metadata, the RPC is cancelled by
  // the server by calling ServerContext::TryCancel() depending on the
  // value:
  //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server
  //   reads any message from the client CANCEL_DURING_PROCESSING: The RPC
  //   is cancelled while the server is reading messages from the client
  //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server reads
  //   all the messages from the client
  int server_try_cancel = internal::GetIntValueFromMetadata(
      kServerTryCancelRequest, context->client_metadata(), DO_NOT_CANCEL);
  if (server_try_cancel == CANCEL_BEFORE_PROCESSING) {
    internal::ServerTryCancelNonblocking(context);
  }

  class Reactor : public grpc::ServerWriteReactor<EchoResponse> {
   public:
    Reactor(CallbackServerContext* ctx, const EchoRequest* request,
            int server_try_cancel)
        : ctx_(ctx), request_(request), server_try_cancel_(server_try_cancel) {
      server_coalescing_api_ = internal::GetIntValueFromMetadata(
          kServerUseCoalescingApi, ctx->client_metadata(), 0);
      server_responses_to_send_ = internal::GetIntValueFromMetadata(
          kServerResponseStreamsToSend, ctx->client_metadata(),
          kServerDefaultResponseStreamsToSend);
      if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
        ctx->TryCancel();
      }
      if (server_try_cancel_ != CANCEL_BEFORE_PROCESSING) {
        if (num_msgs_sent_ < server_responses_to_send_) {
          NextWrite();
        }
      }
      setup_done_ = true;
    }
    void OnDone() override { delete this; }
    void OnCancel() override {
      EXPECT_TRUE(setup_done_);
      EXPECT_TRUE(ctx_->IsCancelled());
      FinishOnce(Status::CANCELLED);
    }
    void OnWriteDone(bool /*ok*/) override {
      if (num_msgs_sent_ < server_responses_to_send_) {
        NextWrite();
      } else if (server_coalescing_api_ != 0) {
        // We would have already done Finish just after the WriteLast
      } else if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
        // Let OnCancel recover this
      } else if (server_try_cancel_ == CANCEL_AFTER_PROCESSING) {
        internal::ServerTryCancelNonblocking(ctx_);
      } else {
        FinishOnce(Status::OK);
      }
    }

   private:
    void FinishOnce(const Status& s) {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        Finish(s);
        finished_ = true;
      }
    }

    void NextWrite() {
      response_.set_message(request_->message() +
                            std::to_string(num_msgs_sent_));
      if (num_msgs_sent_ == server_responses_to_send_ - 1 &&
          server_coalescing_api_ != 0) {
        {
          std::lock_guard<std::mutex> l(finish_mu_);
          if (!finished_) {
            num_msgs_sent_++;
            StartWriteLast(&response_, WriteOptions());
          }
        }
        // If we use WriteLast, we shouldn't wait before attempting Finish
        FinishOnce(Status::OK);
      } else {
        std::lock_guard<std::mutex> l(finish_mu_);
        if (!finished_) {
          num_msgs_sent_++;
          StartWrite(&response_);
        }
      }
    }
    CallbackServerContext* const ctx_;
    const EchoRequest* const request_;
    EchoResponse response_;
    int num_msgs_sent_{0};
    int server_try_cancel_;
    int server_coalescing_api_;
    int server_responses_to_send_;
    std::mutex finish_mu_;
    bool finished_{false};
    bool setup_done_{false};
  };
  return new Reactor(context, request, server_try_cancel);
}

ServerBidiReactor<EchoRequest, EchoResponse>*
CallbackTestServiceImpl::BidiStream(CallbackServerContext* context) {
  class Reactor : public grpc::ServerBidiReactor<EchoRequest, EchoResponse> {
   public:
    explicit Reactor(CallbackServerContext* ctx) : ctx_(ctx) {
      // If 'server_try_cancel' is set in the metadata, the RPC is cancelled by
      // the server by calling ServerContext::TryCancel() depending on the
      // value:
      //   CANCEL_BEFORE_PROCESSING: The RPC is cancelled before the server
      //   reads any message from the client CANCEL_DURING_PROCESSING: The RPC
      //   is cancelled while the server is reading messages from the client
      //   CANCEL_AFTER_PROCESSING: The RPC is cancelled after the server reads
      //   all the messages from the client
      server_try_cancel_ = internal::GetIntValueFromMetadata(
          kServerTryCancelRequest, ctx->client_metadata(), DO_NOT_CANCEL);
      server_write_last_ = internal::GetIntValueFromMetadata(
          kServerFinishAfterNReads, ctx->client_metadata(), 0);
      client_try_cancel_ = static_cast<bool>(internal::GetIntValueFromMetadata(
          kClientTryCancelRequest, ctx->client_metadata(), 0));
      if (server_try_cancel_ == CANCEL_BEFORE_PROCESSING) {
        internal::ServerTryCancelNonblocking(ctx);
      } else {
        if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
          ctx->TryCancel();
        }
        StartRead(&request_);
      }
      setup_done_ = true;
    }
    void OnDone() override {
      {
        // Use the same lock as finish to make sure that OnDone isn't inlined.
        std::lock_guard<std::mutex> l(finish_mu_);
        EXPECT_TRUE(finished_);
        finish_thread_.join();
      }
      delete this;
    }
    void OnCancel() override {
      cancel_notification_.Notify();
      EXPECT_TRUE(setup_done_);
      EXPECT_TRUE(ctx_->IsCancelled());
      FinishOnce(Status::CANCELLED);
    }
    void OnReadDone(bool ok) override {
      if (ok) {
        num_msgs_read_++;
        response_.set_message(request_.message());
        std::lock_guard<std::mutex> l(finish_mu_);
        if (!finished_) {
          if (num_msgs_read_ == server_write_last_) {
            StartWriteLast(&response_, WriteOptions());
            // If we use WriteLast, we shouldn't wait before attempting Finish
          } else {
            StartWrite(&response_);
            return;
          }
        }
      } else if (client_try_cancel_) {
        cancel_notification_.WaitForNotificationWithTimeout(absl::Seconds(10));
        EXPECT_TRUE(ctx_->IsCancelled());
      }

      if (server_try_cancel_ == CANCEL_DURING_PROCESSING) {
        // Let OnCancel handle this
      } else if (server_try_cancel_ == CANCEL_AFTER_PROCESSING) {
        internal::ServerTryCancelNonblocking(ctx_);
      } else {
        FinishOnce(Status::OK);
      }
    }
    void OnWriteDone(bool /*ok*/) override {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        StartRead(&request_);
      }
    }

   private:
    void FinishOnce(const Status& s) {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        finished_ = true;
        // Finish asynchronously to make sure that there are no deadlocks.
        finish_thread_ = std::thread([this, s] {
          std::lock_guard<std::mutex> l(finish_mu_);
          Finish(s);
        });
      }
    }

    CallbackServerContext* const ctx_;
    EchoRequest request_;
    EchoResponse response_;
    int num_msgs_read_{0};
    int server_try_cancel_;
    int server_write_last_;
    std::mutex finish_mu_;
    bool finished_{false};
    bool setup_done_{false};
    std::thread finish_thread_;
    bool client_try_cancel_ = false;
    grpc_core::Notification cancel_notification_;
  };

  return new Reactor(context);
}

}  // namespace testing
}  // namespace grpc
