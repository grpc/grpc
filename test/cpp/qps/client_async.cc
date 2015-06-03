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

#include <cassert>
#include <forward_list>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/async_unary_call.h>
#include <grpc++/client_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/qpstest.grpc.pb.h"
#include "test/cpp/qps/timer.h"
#include "test/cpp/qps/client.h"

namespace grpc {
namespace testing {

typedef std::forward_list<grpc_time> deadline_list;

class ClientRpcContext {
 public:
  ClientRpcContext() {}
  virtual ~ClientRpcContext() {}
  // next state, return false if done. Collect stats when appropriate
  virtual bool RunNextState(bool, Histogram* hist) = 0;
  virtual void StartNewClone() = 0;
  static void* tag(ClientRpcContext* c) { return reinterpret_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return reinterpret_cast<ClientRpcContext*>(t);
  }

  deadline_list::iterator deadline_posn() const {return deadline_posn_;}
  void set_deadline_posn(deadline_list::iterator&& it) {deadline_posn_ = it;}
  virtual void Start() = 0;
 private:
  deadline_list::iterator deadline_posn_;
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(
      TestService::Stub* stub, const RequestType& req,
      std::function<
          std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
              TestService::Stub*, grpc::ClientContext*, const RequestType&)>
          start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextUnaryImpl::RespDone),
        callback_(on_done),
        start_req_(start_req) {
  }
  void Start() GRPC_OVERRIDE {
    start_ = Timer::Now();
    response_reader_ = start_req_(stub_, &context_, req_);
    response_reader_->Finish(&response_, &status_, ClientRpcContext::tag(this));
  }
  ~ClientRpcContextUnaryImpl() GRPC_OVERRIDE {}
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    bool ret = (this->*next_state_)(ok);
    if (!ret) {
      hist->Add((Timer::Now() - start_) * 1e9);
    }
    return ret;
  }

  void StartNewClone() GRPC_OVERRIDE {
    new ClientRpcContextUnaryImpl(stub_, req_, start_req_, callback_);
  }

 private:
  bool RespDone(bool) {
    next_state_ = &ClientRpcContextUnaryImpl::DoCallBack;
    return false;
  }
  bool DoCallBack(bool) {
    callback_(status_, &response_);
    return false;
  }
  grpc::ClientContext context_;
  TestService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextUnaryImpl::*next_state_)(bool);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      TestService::Stub*, grpc::ClientContext*, const RequestType&)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
};

class AsyncClient : public Client {
 public:
  explicit AsyncClient(const ClientConfig& config,
		       std::function<ClientRpcContext*(CompletionQueue*, TestService::Stub*,
					  const SimpleRequest&)> setup_ctx) :
    Client(config), channel_rpc_lock_(config.client_channels()) {

    SetupLoadTest(config, config.async_client_threads());

    for (int i = 0; i < config.async_client_threads(); i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
      if (!closed_loop_) {
        rpc_deadlines_.emplace_back();
        next_channel_.push_back(i % channel_count_);
        issue_allowed_.push_back(true);

        grpc_time next_issue;
        NextIssueTime(i, &next_issue);
        next_issue_.push_back(next_issue);
      }
    }
    if (!closed_loop_) {
      for (auto channel = channels_.begin(); channel != channels_.end();
	   channel++) {
	rpcs_outstanding_.push_back(0);
      }
    }

    int t = 0;
    for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
      for (auto channel = channels_.begin(); channel != channels_.end();
	   channel++) {
	auto* cq = cli_cqs_[t].get();
	t = (t + 1) % cli_cqs_.size();
	ClientRpcContext *ctx = setup_ctx(cq, channel->get_stub(), request_);
	if (closed_loop_) {
	  // only relevant for closed_loop unary, but harmless for
	  // closed_loop streaming
	  ctx->Start();
	}
      }
    }
  }
  virtual ~AsyncClient() {
    for (auto cq = cli_cqs_.begin(); cq != cli_cqs_.end(); cq++) {
      (*cq)->Shutdown();
      void* got_tag;
      bool ok;
      while ((*cq)->Next(&got_tag, &ok)) {
        delete ClientRpcContext::detag(got_tag);
      }
    }
  }

  bool ThreadFunc(Histogram* histogram,
                  size_t thread_idx) GRPC_OVERRIDE GRPC_FINAL {
    void* got_tag;
    bool ok;
    grpc_time deadline, short_deadline;
    if (closed_loop_) {
      deadline = grpc_time_source::now() + std::chrono::seconds(1);
      short_deadline = deadline;
    } else {
      deadline = *(rpc_deadlines_[thread_idx].begin());
      short_deadline = issue_allowed_[thread_idx] ?
	next_issue_[thread_idx] : deadline;
    }

    bool got_event;

    switch (cli_cqs_[thread_idx]->AsyncNext(&got_tag, &ok, short_deadline)) {
      case CompletionQueue::SHUTDOWN: return false;
      case CompletionQueue::TIMEOUT:
	got_event = false;
	break;
      case CompletionQueue::GOT_EVENT:
	got_event = true;
	break;
      default:
        GPR_ASSERT(false);
        break;
    }
   if (grpc_time_source::now() > deadline) {
     // we have missed some 1-second deadline, which is too much                            gpr_log(GPR_INFO, "Missed an RPC deadline, giving up");
     return false;
   }
   if (got_event) {
     ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);
     if (ctx->RunNextState(ok, histogram) == false) {
       // call the callback and then delete it
       rpc_deadlines_[thread_idx].erase_after(ctx->deadline_posn());
       ctx->RunNextState(ok, histogram);
       ctx->StartNewClone();
       delete ctx;
     }
     issue_allowed_[thread_idx] = true; // may be ok now even if it hadn't been
   }
   if (issue_allowed_[thread_idx] &&
       grpc_time_source::now() >= next_issue_[thread_idx]) {
     // Attempt to issue                                                                                                                 
     bool issued = false;
     for (int num_attempts = 0; num_attempts < channel_count_ && !issued;
	  num_attempts++, next_channel_[thread_idx] = (next_channel_[thread_idx]+1)%channel_count_) {
       std::lock_guard<std::mutex>
	 g(channel_rpc_lock_[next_channel_[thread_idx]]);
       if (rpcs_outstanding_[next_channel_[thread_idx]] < max_outstanding_per_channel_) {
	 // do the work to issue
	 rpcs_outstanding_[next_channel_[thread_idx]]++;
	 issued = true;
       }
     }
     if (!issued)
       issue_allowed_[thread_idx] = false;   
   }
   return true;
  }

 private:
  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;

  std::vector<deadline_list> rpc_deadlines_; // per thread deadlines
  std::vector<int> next_channel_; // per thread round-robin channel ctr
  std::vector<bool> issue_allowed_; // may this thread attempt to issue
  std::vector<grpc_time> next_issue_; // when should it issue?

  std::vector<std::mutex> channel_rpc_lock_;
  std::vector<int> rpcs_outstanding_; // per-channel vector
  int max_outstanding_per_channel_;
  int channel_count_;
};

class AsyncUnaryClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncUnaryClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    StartThreads(config.async_client_threads());
  }
  ~AsyncUnaryClient() GRPC_OVERRIDE { EndThreads(); }
private:
  static ClientRpcContext *SetupCtx(CompletionQueue* cq, TestService::Stub* stub,
                       const SimpleRequest& req) {
    auto check_done = [](grpc::Status s, SimpleResponse* response) {};
    auto start_req = [cq](TestService::Stub* stub, grpc::ClientContext* ctx,
                          const SimpleRequest& request) {
      return stub->AsyncUnaryCall(ctx, request, cq);
    };
    return new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
       stub, req, start_req, check_done);
  }
  
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingImpl(
      TestService::Stub* stub, const RequestType& req,
      std::function<std::unique_ptr<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          TestService::Stub*, grpc::ClientContext*, void*)> start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextStreamingImpl::ReqSent),
        callback_(on_done),
        start_req_(start_req),
        start_(Timer::Now()),
        stream_(start_req_(stub_, &context_, ClientRpcContext::tag(this))) {}
  ~ClientRpcContextStreamingImpl() GRPC_OVERRIDE {}
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    return (this->*next_state_)(ok, hist);
  }
  void StartNewClone() GRPC_OVERRIDE {
    new ClientRpcContextStreamingImpl(stub_, req_, start_req_, callback_);
  }
  void Start() GRPC_OVERRIDE {}
 private:
  bool ReqSent(bool ok, Histogram*) { return StartWrite(ok); }
  bool StartWrite(bool ok) {
    if (!ok) {
      return (false);
    }
    start_ = Timer::Now();
    next_state_ = &ClientRpcContextStreamingImpl::WriteDone;
    stream_->Write(req_, ClientRpcContext::tag(this));
    return true;
  }
  bool WriteDone(bool ok, Histogram*) {
    if (!ok) {
      return (false);
    }
    next_state_ = &ClientRpcContextStreamingImpl::ReadDone;
    stream_->Read(&response_, ClientRpcContext::tag(this));
    return true;
  }
  bool ReadDone(bool ok, Histogram* hist) {
    hist->Add((Timer::Now() - start_) * 1e9);
    return StartWrite(ok);
  }
  grpc::ClientContext context_;
  TestService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextStreamingImpl::*next_state_)(bool, Histogram*);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          TestService::Stub*, grpc::ClientContext*, void*)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;
};

class AsyncStreamingClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncStreamingClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    StartThreads(config.async_client_threads());
  }

  ~AsyncStreamingClient() GRPC_OVERRIDE { EndThreads(); }
private:
  static ClientRpcContext *SetupCtx(CompletionQueue* cq, TestService::Stub* stub,
                       const SimpleRequest& req)  {
    auto check_done = [](grpc::Status s, SimpleResponse* response) {};
    auto start_req = [cq](TestService::Stub* stub, grpc::ClientContext* ctx,
                          void* tag) {
      auto stream = stub->AsyncStreamingCall(ctx, cq, tag);
      return stream;
    };
    return new ClientRpcContextStreamingImpl<SimpleRequest, SimpleResponse>(
        stub, req, start_req, check_done);
  }
};

std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncUnaryClient(args));
}
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncStreamingClient(args));
}

}  // namespace testing
}  // namespace grpc
