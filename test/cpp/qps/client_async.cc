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
#include <list>
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
#include <grpc++/client_context.h>

#include "test/cpp/qps/timer.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/proto/benchmarks/services.grpc.pb.h"

namespace grpc {
namespace testing {

typedef std::list<grpc_time> deadline_list;

class ClientRpcContext {
 public:
  explicit ClientRpcContext(int ch) : channel_id_(ch) {}
  virtual ~ClientRpcContext() {}
  // next state, return false if done. Collect stats when appropriate
  virtual bool RunNextState(bool, Histogram* hist) = 0;
  virtual ClientRpcContext* StartNewClone() = 0;
  static void* tag(ClientRpcContext* c) { return reinterpret_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return reinterpret_cast<ClientRpcContext*>(t);
  }

  deadline_list::iterator deadline_posn() const { return deadline_posn_; }
  void set_deadline_posn(const deadline_list::iterator& it) {
    deadline_posn_ = it;
  }
  virtual void Start(CompletionQueue* cq) = 0;
  int channel_id() const { return channel_id_; }

 protected:
  int channel_id_;

 private:
  deadline_list::iterator deadline_posn_;
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(
      int channel_id, BenchmarkService::Stub* stub, const RequestType& req,
      std::function<
          std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
              BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
              CompletionQueue*)> start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : ClientRpcContext(channel_id),
        context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextUnaryImpl::RespDone),
        callback_(on_done),
        start_req_(start_req) {}
  void Start(CompletionQueue* cq) GRPC_OVERRIDE {
    start_ = Timer::Now();
    response_reader_ = start_req_(stub_, &context_, req_, cq);
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

  ClientRpcContext* StartNewClone() GRPC_OVERRIDE {
    return new ClientRpcContextUnaryImpl(channel_id_, stub_, req_, start_req_,
                                         callback_);
  }

 private:
  bool RespDone(bool) {
    next_state_ = &ClientRpcContextUnaryImpl::DoCallBack;
    return false;
  }
  bool DoCallBack(bool) {
    callback_(status_, &response_);
    return true;  // we're done, this'll be ignored
  }
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextUnaryImpl::*next_state_)(bool);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
      CompletionQueue*)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
};

typedef std::forward_list<ClientRpcContext*> context_list;

class AsyncClient : public Client {
 public:
  explicit AsyncClient(
      const ClientConfig& config,
      std::function<ClientRpcContext*(int, BenchmarkService::Stub*,
                                      const SimpleRequest&)> setup_ctx)
      : Client(config),
        channel_lock_(new std::mutex[config.client_channels()]),
        contexts_(config.client_channels()),
        max_outstanding_per_channel_(config.outstanding_rpcs_per_channel()),
        channel_count_(config.client_channels()),
        pref_channel_inc_(config.async_client_threads()) {
    SetupLoadTest(config, config.async_client_threads());

    for (int i = 0; i < config.async_client_threads(); i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
      if (!closed_loop_) {
        rpc_deadlines_.emplace_back();
        next_channel_.push_back(i % channel_count_);
        issue_allowed_.emplace_back(true);

        grpc_time next_issue;
        NextIssueTime(i, &next_issue);
        next_issue_.push_back(next_issue);
      }
    }

    int t = 0;
    for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
      for (int ch = 0; ch < channel_count_; ch++) {
        auto* cq = cli_cqs_[t].get();
        t = (t + 1) % cli_cqs_.size();
        auto ctx = setup_ctx(ch, channels_[ch].get_stub(), request_);
        if (closed_loop_) {
          ctx->Start(cq);
        } else {
          contexts_[ch].push_front(ctx);
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
    // Now clear out all the pre-allocated idle contexts
    for (int ch = 0; ch < channel_count_; ch++) {
      while (!contexts_[ch].empty()) {
        // Get an idle context from the front of the list
        auto* ctx = *(contexts_[ch].begin());
        contexts_[ch].pop_front();
        delete ctx;
      }
    }
    delete[] channel_lock_;
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
      if (rpc_deadlines_[thread_idx].empty()) {
        deadline = grpc_time_source::now() + std::chrono::seconds(1);
      } else {
        deadline = *(rpc_deadlines_[thread_idx].begin());
      }
      short_deadline =
          issue_allowed_[thread_idx] ? next_issue_[thread_idx] : deadline;
    }

    bool got_event;

    switch (cli_cqs_[thread_idx]->AsyncNext(&got_tag, &ok, short_deadline)) {
      case CompletionQueue::SHUTDOWN:
        return false;
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
    if (got_event) {
      ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);
      if (ctx->RunNextState(ok, histogram) == false) {
        // call the callback and then clone the ctx
        ctx->RunNextState(ok, histogram);
        ClientRpcContext* clone_ctx = ctx->StartNewClone();
        if (closed_loop_) {
          clone_ctx->Start(cli_cqs_[thread_idx].get());
        } else {
          // Remove the entry from the rpc deadlines list
          rpc_deadlines_[thread_idx].erase(ctx->deadline_posn());
          // Put the clone_ctx in the list of idle contexts for this channel
          // Under lock
          int ch = clone_ctx->channel_id();
          std::lock_guard<std::mutex> g(channel_lock_[ch]);
          contexts_[ch].push_front(clone_ctx);
        }
        // delete the old version
        delete ctx;
      }
      if (!closed_loop_)
        issue_allowed_[thread_idx] =
            true;  // may be ok now even if it hadn't been
    }
    if (!closed_loop_ && issue_allowed_[thread_idx] &&
        grpc_time_source::now() >= next_issue_[thread_idx]) {
      // Attempt to issue
      bool issued = false;
      for (int num_attempts = 0, channel_attempt = next_channel_[thread_idx];
           num_attempts < channel_count_ && !issued; num_attempts++) {
        bool can_issue = false;
        ClientRpcContext* ctx = nullptr;
        {
          std::lock_guard<std::mutex> g(channel_lock_[channel_attempt]);
          if (!contexts_[channel_attempt].empty()) {
            // Get an idle context from the front of the list
            ctx = *(contexts_[channel_attempt].begin());
            contexts_[channel_attempt].pop_front();
            can_issue = true;
          }
        }
        if (can_issue) {
          // do the work to issue
          rpc_deadlines_[thread_idx].emplace_back(grpc_time_source::now() +
                                                  std::chrono::seconds(1));
          auto it = rpc_deadlines_[thread_idx].end();
          --it;
          ctx->set_deadline_posn(it);
          ctx->Start(cli_cqs_[thread_idx].get());
          issued = true;
          // If we did issue, then next time, try our thread's next
          // preferred channel
          next_channel_[thread_idx] += pref_channel_inc_;
          if (next_channel_[thread_idx] >= channel_count_)
            next_channel_[thread_idx] = (thread_idx % channel_count_);
        } else {
          // Do a modular increment of channel attempt if we couldn't issue
          channel_attempt = (channel_attempt + 1) % channel_count_;
        }
      }
      if (issued) {
        // We issued one; see when we can issue the next
        grpc_time next_issue;
        NextIssueTime(thread_idx, &next_issue);
        next_issue_[thread_idx] = next_issue;
      } else {
        issue_allowed_[thread_idx] = false;
      }
    }
    return true;
  }

 private:
  class boolean {  // exists only to avoid data-race on vector<bool>
   public:
    boolean() : val_(false) {}
    boolean(bool b) : val_(b) {}
    operator bool() const { return val_; }
    boolean& operator=(bool b) {
      val_ = b;
      return *this;
    }

   private:
    bool val_;
  };
  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;

  std::vector<deadline_list> rpc_deadlines_;  // per thread deadlines
  std::vector<int> next_channel_;       // per thread round-robin channel ctr
  std::vector<boolean> issue_allowed_;  // may this thread attempt to issue
  std::vector<grpc_time> next_issue_;   // when should it issue?

  std::mutex*
      channel_lock_;  // a vector, but avoid std::vector for old compilers
  std::vector<context_list> contexts_;  // per-channel list of idle contexts
  int max_outstanding_per_channel_;
  int channel_count_;
  int pref_channel_inc_;
};

class AsyncUnaryClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncUnaryClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    StartThreads(config.async_client_threads());
  }
  ~AsyncUnaryClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void CheckDone(grpc::Status s, SimpleResponse* response) {}
  static std::unique_ptr<grpc::ClientAsyncResponseReader<SimpleResponse>>
  StartReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
           const SimpleRequest& request, CompletionQueue* cq) {
    return stub->AsyncUnaryCall(ctx, request, cq);
  };
  static ClientRpcContext* SetupCtx(int channel_id,
                                    BenchmarkService::Stub* stub,
                                    const SimpleRequest& req) {
    return new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
        channel_id, stub, req, AsyncUnaryClient::StartReq,
        AsyncUnaryClient::CheckDone);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingImpl(
      int channel_id, BenchmarkService::Stub* stub, const RequestType& req,
      std::function<std::unique_ptr<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*,
          void*)> start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : ClientRpcContext(channel_id),
        context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextStreamingImpl::ReqSent),
        callback_(on_done),
        start_req_(start_req),
        start_(Timer::Now()) {}
  ~ClientRpcContextStreamingImpl() GRPC_OVERRIDE {}
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    return (this->*next_state_)(ok, hist);
  }
  ClientRpcContext* StartNewClone() GRPC_OVERRIDE {
    return new ClientRpcContextStreamingImpl(channel_id_, stub_, req_,
                                             start_req_, callback_);
  }
  void Start(CompletionQueue* cq) GRPC_OVERRIDE {
    stream_ = start_req_(stub_, &context_, cq, ClientRpcContext::tag(this));
  }

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
  BenchmarkService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextStreamingImpl::*next_state_)(bool, Histogram*);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*,
          void*)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;
};

class AsyncStreamingClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncStreamingClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    // async streaming currently only supports closed loop
    GPR_ASSERT(closed_loop_);

    StartThreads(config.async_client_threads());
  }

  ~AsyncStreamingClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void CheckDone(grpc::Status s, SimpleResponse* response) {}
  static std::unique_ptr<
      grpc::ClientAsyncReaderWriter<SimpleRequest, SimpleResponse>>
  StartReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
           CompletionQueue* cq, void* tag) {
    auto stream = stub->AsyncStreamingCall(ctx, cq, tag);
    return stream;
  };
  static ClientRpcContext* SetupCtx(int channel_id,
                                    BenchmarkService::Stub* stub,
                                    const SimpleRequest& req) {
    return new ClientRpcContextStreamingImpl<SimpleRequest, SimpleResponse>(
        channel_id, stub, req, AsyncStreamingClient::StartReq,
        AsyncStreamingClient::CheckDone);
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
