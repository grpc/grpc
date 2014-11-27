/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/cpp/stream/stream_context.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/cpp/rpc_method.h"
#include "src/cpp/proto/proto_utils.h"
#include "src/cpp/util/time.h"
#include <grpc++/client_context.h>
#include <grpc++/config.h>
#include <google/protobuf/message.h>

namespace grpc {

// Client only ctor
StreamContext::StreamContext(const RpcMethod& method, ClientContext* context,
                             const google::protobuf::Message* request,
                             google::protobuf::Message* result)
    : is_client_(true),
      method_(&method),
      context_(context),
      request_(request),
      result_(result),
      invoke_ev_(nullptr),
      read_ev_(nullptr),
      write_ev_(nullptr),
      reading_(false),
      writing_(false),
      got_read_(false),
      got_write_(false),
      peer_halfclosed_(false),
      self_halfclosed_(false),
      stream_finished_(false),
      waiting_(false) {
  GPR_ASSERT(method_->method_type() != RpcMethod::RpcType::NORMAL_RPC);
}

StreamContext::~StreamContext() { cq_poller_.join(); }

void StreamContext::PollingLoop() {
  grpc_event* ev = nullptr;
  gpr_timespec absolute_deadline;
  AbsoluteDeadlineTimepoint2Timespec(context_->absolute_deadline(),
                                     &absolute_deadline);
  std::condition_variable* cv_to_notify = nullptr;
  std::unique_lock<std::mutex> lock(mu_, std::defer_lock);
  while (1) {
    cv_to_notify = nullptr;
    lock.lock();
    if (stream_finished_ && !reading_ && !writing_) {
      return;
    }
    lock.unlock();
    ev = grpc_completion_queue_next(context_->cq(), absolute_deadline);
    lock.lock();
    if (!ev) {
      stream_finished_ = true;
      final_status_ = Status(StatusCode::DEADLINE_EXCEEDED);
      std::condition_variable* cvs[3] = {reading_ ? &read_cv_ : nullptr,
                                         writing_ ? &write_cv_ : nullptr,
                                         waiting_ ? &finish_cv_ : nullptr};
      got_read_ = reading_;
      got_write_ = writing_;
      read_ev_ = nullptr;
      write_ev_ = nullptr;
      lock.unlock();
      for (int i = 0; i < 3; i++) {
        if (cvs[i]) cvs[i]->notify_one();
      }
      break;
    }
    switch (ev->type) {
      case GRPC_READ:
        GPR_ASSERT(reading_);
        got_read_ = true;
        read_ev_ = ev;
        cv_to_notify = &read_cv_;
        reading_ = false;
        break;
      case GRPC_FINISH_ACCEPTED:
      case GRPC_WRITE_ACCEPTED:
        got_write_ = true;
        write_ev_ = ev;
        cv_to_notify = &write_cv_;
        writing_ = false;
        break;
      case GRPC_FINISHED: {
        grpc::string error_details(
            ev->data.finished.details ? ev->data.finished.details : "");
        final_status_ = Status(static_cast<StatusCode>(ev->data.finished.code),
                               error_details);
        grpc_event_finish(ev);
        stream_finished_ = true;
        if (waiting_) {
          cv_to_notify = &finish_cv_;
        }
        break;
      }
      case GRPC_INVOKE_ACCEPTED:
        invoke_ev_ = ev;
        cv_to_notify = &invoke_cv_;
        break;
      case GRPC_CLIENT_METADATA_READ:
        grpc_event_finish(ev);
        break;
      default:
        grpc_event_finish(ev);
        // not handling other types now
        gpr_log(GPR_ERROR, "unknown event type");
        abort();
    }
    lock.unlock();
    if (cv_to_notify) {
      cv_to_notify->notify_one();
    }
  }
}

void StreamContext::Start(bool buffered) {
  // TODO(yangg) handle metadata send path
  int flag = buffered ? GRPC_WRITE_BUFFER_HINT : 0;
  grpc_call_error error = grpc_call_start_invoke(
      context_->call(), context_->cq(), this, this, this, flag);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // kicks off the poller thread
  cq_poller_ = std::thread(&StreamContext::PollingLoop, this);
  std::unique_lock<std::mutex> lock(mu_);
  while (!invoke_ev_) {
    invoke_cv_.wait(lock);
  }
  lock.unlock();
  GPR_ASSERT(invoke_ev_->data.invoke_accepted == GRPC_OP_OK);
  grpc_event_finish(invoke_ev_);
}

namespace {
// Wait for got_event with event_cv protected by mu, return event.
grpc_event* WaitForEvent(bool* got_event, std::condition_variable* event_cv,
                         std::mutex* mu, grpc_event** event) {
  std::unique_lock<std::mutex> lock(*mu);
  while (*got_event == false) {
    event_cv->wait(lock);
  }
  *got_event = false;
  return *event;
}
}  // namespace

bool StreamContext::Read(google::protobuf::Message* msg) {
  std::unique_lock<std::mutex> lock(mu_);
  if (stream_finished_) {
    peer_halfclosed_ = true;
    return false;
  }
  reading_ = true;
  lock.unlock();

  grpc_call_error err = grpc_call_start_read(context_->call(), this);
  GPR_ASSERT(err == GRPC_CALL_OK);

  grpc_event* ev = WaitForEvent(&got_read_, &read_cv_, &mu_, &read_ev_);
  if (!ev) {
    return false;
  }
  GPR_ASSERT(ev->type == GRPC_READ);
  bool ret = true;
  if (ev->data.read) {
    if (!DeserializeProto(ev->data.read, msg)) {
      ret = false;  // parse error
                    // TODO(yangg) cancel the stream.
    }
  } else {
    ret = false;
    peer_halfclosed_ = true;
  }
  grpc_event_finish(ev);
  return ret;
}

bool StreamContext::Write(const google::protobuf::Message* msg, bool is_last) {
  bool ret = true;
  grpc_event* ev = nullptr;

  std::unique_lock<std::mutex> lock(mu_);
  if (stream_finished_) {
    self_halfclosed_ = true;
    return false;
  }
  writing_ = true;
  lock.unlock();

  if (msg) {
    grpc_byte_buffer* out_buf = nullptr;
    if (!SerializeProto(*msg, &out_buf)) {
      FinishStream(Status(StatusCode::INVALID_ARGUMENT,
                          "Failed to serialize request proto"),
                   true);
      return false;
    }
    int flag = is_last ? GRPC_WRITE_BUFFER_HINT : 0;
    grpc_call_error err =
        grpc_call_start_write(context_->call(), out_buf, this, flag);
    grpc_byte_buffer_destroy(out_buf);
    GPR_ASSERT(err == GRPC_CALL_OK);

    ev = WaitForEvent(&got_write_, &write_cv_, &mu_, &write_ev_);
    if (!ev) {
      return false;
    }
    GPR_ASSERT(ev->type == GRPC_WRITE_ACCEPTED);

    ret = ev->data.write_accepted == GRPC_OP_OK;
    grpc_event_finish(ev);
  }
  if (is_last) {
    grpc_call_error err = grpc_call_writes_done(context_->call(), this);
    GPR_ASSERT(err == GRPC_CALL_OK);
    ev = WaitForEvent(&got_write_, &write_cv_, &mu_, &write_ev_);
    if (!ev) {
      return false;
    }
    GPR_ASSERT(ev->type == GRPC_FINISH_ACCEPTED);
    grpc_event_finish(ev);
    self_halfclosed_ = true;
  }
  return ret;
}

const Status& StreamContext::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  // TODO(yangg) if not halfclosed cancel the stream
  GPR_ASSERT(self_halfclosed_);
  GPR_ASSERT(peer_halfclosed_);
  GPR_ASSERT(!waiting_);
  waiting_ = true;
  while (!stream_finished_) {
    finish_cv_.wait(lock);
  }
  return final_status_;
}

void StreamContext::FinishStream(const Status& status, bool send) { return; }

}  // namespace grpc
