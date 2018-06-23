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

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"
#include "src/cpp/server/load_reporter/load_reporter_async_service_impl.h"

namespace grpc {
namespace load_reporter {

::grpc_core::DebugOnlyTraceFlag grpc_trace_server_load_reporting_refcount(
    false, "server_load_reporting_refcount");

LoadReporterAsyncServiceImpl::LoadReporterAsyncServiceImpl(
    std::unique_ptr<ServerCompletionQueue> cq)
    : cq_(std::move(cq)) {
  thread_ = std::unique_ptr<::grpc_core::Thread>(
      new ::grpc_core::Thread("server_load_reporting", Work, this));
  std::unique_ptr<CpuStatsProvider> cpu_stats_provider = nullptr;
#if defined(GPR_LINUX) || defined(GPR_WINDOWS) || defined(GPR_APPLE)
  cpu_stats_provider.reset(new CpuStatsProviderDefaultImpl());
#endif
  load_reporter_ = std::unique_ptr<LoadReporter>(new LoadReporter(
      kFeedbackSampleWindowSeconds,
      std::unique_ptr<CensusViewProvider>(new CensusViewProviderDefaultImpl()),
      std::move(cpu_stats_provider)));
}

LoadReporterAsyncServiceImpl::~LoadReporterAsyncServiceImpl() {
  // We will reach here after the server starts shutting down.
  shutdown_ = true;
  {
    std::unique_lock<std::mutex> lock(cq_shutdown_mu_);
    cq_->Shutdown();
  }
  next_fetch_and_sample_alarm_.Cancel();
  thread_->Join();
}

void LoadReporterAsyncServiceImpl::ScheduleNextFetchAndSample() {
  auto next_step = new std::function<void(bool)>(
      std::bind(&LoadReporterAsyncServiceImpl::FetchAndSample, this,
                std::placeholders::_1));
  auto next_fetch_and_sample_time =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_millis(kFetchAndSampleIntervalSeconds * 1000,
                                        GPR_TIMESPAN));
  {
    std::unique_lock<std::mutex> lock(cq_shutdown_mu_);
    if (shutdown_) return;
    next_fetch_and_sample_alarm_.Set(cq_.get(), next_fetch_and_sample_time,
                                     next_step);
  }
  gpr_log(GPR_DEBUG, "[LRS %p] Next fetch-and-sample scheduled.", this);
}

void LoadReporterAsyncServiceImpl::FetchAndSample(bool ok) {
  if (!ok) {
    gpr_log(GPR_INFO, "[LRS %p] Fetch-and-sample is stopped.", this);
    return;
  }
  gpr_log(GPR_DEBUG, "[LRS %p] Starting a fetch-and-sample...", this);
  load_reporter_->FetchAndSample();
  ScheduleNextFetchAndSample();
}

void LoadReporterAsyncServiceImpl::Work(void* arg) {
  LoadReporterAsyncServiceImpl* service =
      reinterpret_cast<LoadReporterAsyncServiceImpl*>(arg);
  service->FetchAndSample(true /* ok */);
  // TODO(juanlishen): This is a workaround to wait for the cq to be ready. Need
  // to figure out why cq is not ready after service starts.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  new ReportLoadHandler(service->cq_.get(), service,
                        service->load_reporter_.get());
  void* tag;
  bool ok;
  while (true) {
    if (!service->cq_->Next(&tag, &ok)) {
      // The completion queue is shutting down.
      GPR_ASSERT(service->shutdown_);
      break;
    }
    auto next_step = static_cast<std::function<void(bool)>*>(tag);
    (*next_step)(ok);
    delete next_step;
  }
}

void LoadReporterAsyncServiceImpl::StartThread() { thread_->Start(); }

LoadReporterAsyncServiceImpl::ReportLoadHandler::ReportLoadHandler(
    ServerCompletionQueue* cq, LoadReporterAsyncServiceImpl* service,
    LoadReporter* load_reporter)
    : cq_(cq),
      service_(service),
      load_reporter_(load_reporter),
      stream_(&ctx_),
      call_status_(WAITING_FOR_DELIVERY) {
  gpr_ref_init(&refs_, 1);
  auto on_done = new std::function<void(bool)>(std::bind(
      &ReportLoadHandler::OnDoneNotified, this, std::placeholders::_1));
  auto next_step = new std::function<void(bool)>(std::bind(
      &ReportLoadHandler::OnRequestDelivered, this, std::placeholders::_1));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      ShutdownAndUnref("ctor_unfinished");
      return;
    }
    // Re-use the initial ref for on_done.
    ctx_.AsyncNotifyWhenDone(on_done);
    Ref(DEBUG_LOCATION, "OnRequestDelivered");
    service->RequestReportLoad(&ctx_, &stream_, cq_, cq_, next_step);
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnRequestDelivered(
    bool ok) {
  if (ok) call_status_ = DELIVERED;
  if (!ok || shutdown_) {
    // The value of ok being false means that the server is shutting down.
    ShutdownAndUnref("OnRequestDelivered");
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  new ReportLoadHandler(cq_, service_, load_reporter_);
  auto next_step = new std::function<void(bool)>(
      std::bind(&ReportLoadHandler::OnReadDone, this, std::placeholders::_1));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      ShutdownAndUnref("OnRequestDelivered");
      return;
    }
    // Re-use the OnRequestDelivered ref.
    stream_.Read(&request_, next_step);
  }
  // LB ID is unique for each load reporting stream.
  lb_id_ = load_reporter_->GenerateLbId();
  gpr_log(GPR_INFO,
          "[LRS %p] Call request delivered (lb_id_: %s, handler: %p). "
          "Start reading the initial request...",
          service_, lb_id_.c_str(), this);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnReadDone(bool ok) {
  if (!ok || shutdown_) {
    if (!ok && call_status_ < INITIAL_REQUEST_RECEIVED) {
      // The client may have half-closed the stream or the stream is broken.
      gpr_log(GPR_INFO,
              "[LRS %p] Failed reading the initial request from the stream "
              "(lb_id_: %s, handler: %p, done_notified: %d, is_cancelled: %d).",
              service_, lb_id_.c_str(), this, static_cast<int>(done_notified_),
              static_cast<int>(is_cancelled_));
    }
    ShutdownAndUnref("OnReadDone");
    return;
  }
  // We only receive one request, which is the initial request.
  if (call_status_ < INITIAL_REQUEST_RECEIVED) {
    if (!request_.has_initial_request()) {
      ShutdownAndUnref("OnReadDone+initial_request_not_found");
    } else {
      call_status_ = INITIAL_REQUEST_RECEIVED;
      const auto& initial_request = request_.initial_request();
      load_balanced_hostname_ = initial_request.load_balanced_hostname();
      load_key_ = initial_request.load_key();
      load_reporter_->ReportStreamCreated(load_balanced_hostname_, lb_id_,
                                          load_key_);
      const auto& load_report_interval = initial_request.load_report_interval();
      load_report_interval_ms_ =
          static_cast<uint64_t>(load_report_interval.seconds() * 1000 +
                                load_report_interval.nanos() / 1000);
      gpr_log(
          GPR_INFO,
          "[LRS %p] Initial request received. Start load reporting (load "
          "balanced host: %s, interval: %lu ms, lb_id_: %s, handler: %p)...",
          service_, load_balanced_hostname_.c_str(), load_report_interval_ms_,
          lb_id_.c_str(), this);
      Ref(DEBUG_LOCATION, "SendReport");
      SendReport(true /* ok */);
      auto next_step = new std::function<void(bool)>(std::bind(
          &ReportLoadHandler::OnReadDone, this, std::placeholders::_1));
      // Expect this read to fail.
      {
        std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
        if (service_->shutdown_) {
          lock.release()->unlock();
          ShutdownAndUnref("OnReadDone");
          return;
        }
        // Re-use ref.
        stream_.Read(&request_, next_step);
      }
    }
  } else {
    // Another request received! This violates the spec.
    gpr_log(GPR_ERROR,
            "[LRS %p] Another request received (lb_id_: %s, handler: %p).",
            service_, lb_id_.c_str(), this);
    ShutdownAndUnref("OnReadDone+second_request");
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::ScheduleNextReport(
    bool ok) {
  if (!ok || shutdown_) {
    ShutdownAndUnref("ScheduleNextReport");
    return;
  }
  auto next_step = new std::function<void(bool)>(
      std::bind(&ReportLoadHandler::SendReport, this, std::placeholders::_1));
  auto next_report_time = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(load_report_interval_ms_, GPR_TIMESPAN));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      ShutdownAndUnref("ScheduleNextReport");
      return;
    }
    // Re-use ref.
    next_report_alarm_.Set(cq_, next_report_time, next_step);
  }
  gpr_log(GPR_DEBUG,
          "[LRS %p] Next load report scheduled (lb_id_: %s, handler: %p).",
          service_, lb_id_.c_str(), this);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::SendReport(bool ok) {
  if (!ok || shutdown_) {
    ShutdownAndUnref("SendReport");
    return;
  }
  ::grpc::lb::v1::LoadReportResponse response;
  auto loads = load_reporter_->GenerateLoads(load_balanced_hostname_, lb_id_);
  response.mutable_load()->Swap(&loads);
  auto feedback = load_reporter_->GenerateLoadBalancingFeedback();
  response.mutable_load_balancing_feedback()->Swap(&feedback);
  if (call_status_ < INITIAL_RESPONSE_SENT) {
    auto initial_response = response.mutable_initial_response();
    initial_response->set_load_balancer_id(lb_id_);
    initial_response->set_implementation_id(
        ::grpc::lb::v1::InitialLoadReportResponse::CPP);
    initial_response->set_server_version(kVersion);
    call_status_ = INITIAL_RESPONSE_SENT;
  }
  auto next_step = new std::function<void(bool)>(std::bind(
      &ReportLoadHandler::ScheduleNextReport, this, std::placeholders::_1));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      ShutdownAndUnref("SendReport");
      return;
    }
    // Re-use ref.
    stream_.Write(response, next_step);
    gpr_log(GPR_INFO,
            "[LRS %p] Sending load report (lb_id_: %s, handler: %p, loads "
            "count: %d)...",
            service_, lb_id_.c_str(), this, response.load().size());
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::ShutdownAndUnref(
    const char* reason) {
  if (!shutdown_) {
    gpr_log(GPR_INFO,
            "[LRS %p] Shutting down the handler (lb_id_: %s, handler: %p, "
            "reason: %s).",
            service_, lb_id_.c_str(), this, reason);
    shutdown_ = true;
    if (call_status_ >= INITIAL_REQUEST_RECEIVED) {
      load_reporter_->ReportStreamClosed(load_balanced_hostname_, lb_id_);
      next_report_alarm_.Cancel();
    }
  }
  // OnRequestDelivered() may be called after OnDoneNotified(), so we need try
  // to Finish() every time we are in ShutdownAndUnref().
  if (call_status_ >= DELIVERED && call_status_ < FINISH_CALLED) {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (!service_->shutdown_) {
      auto next_step = new std::function<void(bool)>(std::bind(
          &ReportLoadHandler::OnFinishDone, this, std::placeholders::_1));
      Ref(DEBUG_LOCATION, "OnFinishDone");
      // TODO(juanlishen): Maybe add a message proto for the client to
      // explicitly cancel the stream so that we can return OK status in such
      // cases.
      stream_.Finish(Status::CANCELLED, next_step);
      call_status_ = FINISH_CALLED;
    }
  }
  Unref(DEBUG_LOCATION, reason);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnFinishDone(bool ok) {
  if (ok) {
    gpr_log(GPR_INFO,
            "[LRS %p] Load reporting finished (lb_id_: %s, handler: %p).",
            service_, lb_id_.c_str(), this);
  }
  Unref(DEBUG_LOCATION, "OnFinishDone");
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnDoneNotified(bool ok) {
  GPR_ASSERT(ok);
  done_notified_ = true;
  if (ctx_.IsCancelled()) {
    is_cancelled_ = true;
  }
  gpr_log(GPR_INFO,
          "[LRS %p] Load reporting call is notified done (handler: %p, "
          "is_cancelled: %d).",
          service_, this, static_cast<int>(is_cancelled_));
  ShutdownAndUnref("OnDoneNotified");
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Ref(
    const grpc_core::DebugLocation& location, const char* reason) {
#ifndef NDEBUG
  if (location.Log() && grpc_trace_server_load_reporting_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
    gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
            grpc_trace_server_load_reporting_refcount.name(), this,
            location.file(), location.line(), old_refs, old_refs + 1, reason);
  }
#endif
  gpr_ref(&refs_);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Unref(
    const grpc_core::DebugLocation& location, const char* reason) {
#ifndef NDEBUG
  if (location.Log() && grpc_trace_server_load_reporting_refcount.enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
    gpr_log(GPR_INFO, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
            grpc_trace_server_load_reporting_refcount.name(), this,
            location.file(), location.line(), old_refs, old_refs - 1, reason);
  }
#endif
  if (gpr_unref(&refs_)) {
    delete this;
  }
}

}  // namespace load_reporter
}  // namespace grpc
