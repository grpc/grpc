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

#include "src/cpp/server/load_reporter/load_reporter_async_service_impl.h"

namespace grpc {
namespace load_reporter {

LoadReporterAsyncServiceImpl::LoadReporterAsyncServiceImpl(
    std::unique_ptr<ServerCompletionQueue> cq)
    : cq_(std::move(cq)),
      next_fetch_and_sample_(
          std::bind(&LoadReporterAsyncServiceImpl::FetchAndSample, this,
                    std::placeholders::_1)) {
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
  auto next_fetch_and_sample_time =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_millis(kFetchAndSampleIntervalSeconds * 1000,
                                        GPR_TIMESPAN));
  {
    std::unique_lock<std::mutex> lock(cq_shutdown_mu_);
    if (shutdown_) return;
    next_fetch_and_sample_alarm_.Set(cq_.get(), next_fetch_and_sample_time,
                                     &next_fetch_and_sample_);
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
  std::shared_ptr<ReportLoadHandler> handler =
      std::make_shared<ReportLoadHandler>(service->cq_.get(), service,
                                          service->load_reporter_.get());
  handler->Start();
  // Orphan the handler.
  handler.reset();
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
      call_status_(WAITING_FOR_DELIVERY) {}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Start() {
  std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
  if (service_->shutdown_) return;
  // AsyncNotifyWhenDone() needs to be called before the call starts, but the
  // tag may not pop out if the call never starts. Please refer to
  // https://github.com/grpc/grpc/issues/10136.
  on_done_notified_ = std::function<void(bool)>(
      std::bind(&ReportLoadHandler::OnDoneNotified, shared_from_this(),
                &on_done_notified_, std::placeholders::_1));
  next_inbound_ = std::function<void(bool)>(
      std::bind(&ReportLoadHandler::OnRequestDelivered, shared_from_this(),
                &next_inbound_, std::placeholders::_1));
  ctx_.AsyncNotifyWhenDone(&on_done_notified_);
  service_->RequestReportLoad(&ctx_, &stream_, cq_, cq_, &next_inbound_);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnRequestDelivered(
    std::function<void(bool)>* func, bool ok) {
  if (ok) call_status_ = DELIVERED;
  if (!ok || shutdown_) {
    // The value of ok being false means that the server is shutting down.
    Shutdown(func, "OnRequestDelivered");
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  std::shared_ptr<ReportLoadHandler> handler =
      std::make_shared<ReportLoadHandler>(cq_, service_, load_reporter_);
  handler->Start();
  // Orphan the handler.
  handler.reset();
  *func = std::function<void(bool)>(std::bind(&ReportLoadHandler::OnReadDone,
                                              shared_from_this(), func,
                                              std::placeholders::_1));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      Shutdown(func, "OnRequestDelivered");
      return;
    }
    stream_.Read(&request_, func);
  }
  // LB ID is unique for each load reporting stream.
  lb_id_ = load_reporter_->GenerateLbId();
  gpr_log(GPR_INFO,
          "[LRS %p] Call request delivered (lb_id_: %s, handler: %p). "
          "Start reading the initial request...",
          service_, lb_id_.c_str(), this);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnReadDone(
    std::function<void(bool)>* func, bool ok) {
  if (!ok || shutdown_) {
    if (!ok && call_status_ < INITIAL_REQUEST_RECEIVED) {
      // The client may have half-closed the stream or the stream is broken.
      gpr_log(GPR_INFO,
              "[LRS %p] Failed reading the initial request from the stream "
              "(lb_id_: %s, handler: %p, done_notified: %d, is_cancelled: %d).",
              service_, lb_id_.c_str(), this, static_cast<int>(done_notified_),
              static_cast<int>(is_cancelled_));
    }
    Shutdown(func, "OnReadDone");
    return;
  }
  // We only receive one request, which is the initial request.
  if (call_status_ < INITIAL_REQUEST_RECEIVED) {
    if (!request_.has_initial_request()) {
      Shutdown(func, "OnReadDone+initial_request_not_found");
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
      SendReport(&next_outbound_, true /* ok */);
      // Expect this read to fail.
      {
        std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
        if (service_->shutdown_) {
          lock.release()->unlock();
          Shutdown(func, "OnReadDone");
          return;
        }
        stream_.Read(&request_, func);
      }
    }
  } else {
    // Another request received! This violates the spec.
    gpr_log(GPR_ERROR,
            "[LRS %p] Another request received (lb_id_: %s, handler: %p).",
            service_, lb_id_.c_str(), this);
    Shutdown(func, "OnReadDone+second_request");
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::ScheduleNextReport(
    std::function<void(bool)>* func, bool ok) {
  if (!ok || shutdown_) {
    Shutdown(func, "ScheduleNextReport");
    return;
  }
  *func = std::function<void(bool)>(std::bind(&ReportLoadHandler::SendReport,
                                              shared_from_this(), func,
                                              std::placeholders::_1));
  auto next_report_time = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(load_report_interval_ms_, GPR_TIMESPAN));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      Shutdown(func, "ScheduleNextReport");
      return;
    }
    next_report_alarm_.Set(cq_, next_report_time, func);
  }
  gpr_log(GPR_DEBUG,
          "[LRS %p] Next load report scheduled (lb_id_: %s, handler: %p).",
          service_, lb_id_.c_str(), this);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::SendReport(
    std::function<void(bool)>* func, bool ok) {
  if (!ok || shutdown_) {
    Shutdown(func, "SendReport");
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
  *func = std::function<void(bool)>(
      std::bind(&ReportLoadHandler::ScheduleNextReport, shared_from_this(),
                func, std::placeholders::_1));
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.release()->unlock();
      Shutdown(func, "SendReport");
      return;
    }
    stream_.Write(response, func);
    gpr_log(GPR_INFO,
            "[LRS %p] Sending load report (lb_id_: %s, handler: %p, loads "
            "count: %d)...",
            service_, lb_id_.c_str(), this, response.load().size());
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnDoneNotified(
    std::function<void(bool)>* func, bool ok) {
  GPR_ASSERT(ok);
  done_notified_ = true;
  if (ctx_.IsCancelled()) {
    is_cancelled_ = true;
  }
  gpr_log(GPR_INFO,
          "[LRS %p] Load reporting call is notified done (handler: %p, "
          "is_cancelled: %d).",
          service_, this, static_cast<int>(is_cancelled_));
  Shutdown(func, "OnDoneNotified");
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Shutdown(
    std::function<void(bool)>* from_func, const char* reason) {
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
  // OnRequestDelivered() may be called after OnDoneNotified(), so we need to
  // try to Finish() every time we are in Shutdown().
  if (call_status_ >= DELIVERED && call_status_ < FINISH_CALLED) {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (!service_->shutdown_) {
      on_finish_done_ = std::function<void(bool)>(
          std::bind(&ReportLoadHandler::OnFinishDone, shared_from_this(),
                    &on_finish_done_, std::placeholders::_1));
      // TODO(juanlishen): Maybe add a message proto for the client to
      // explicitly cancel the stream so that we can return OK status in such
      // cases.
      stream_.Finish(Status::CANCELLED, &on_finish_done_);
      call_status_ = FINISH_CALLED;
    }
  }
  // TODO(juanlishen): This dummy shared_ptr is to keep a use count of the
  // handler until the method returns so that the function object can release
  // its ownership safely.
  auto dummy = shared_from_this();
  *from_func = nullptr;
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnFinishDone(
    std::function<void(bool)>* func, bool ok) {
  if (ok) {
    gpr_log(GPR_INFO,
            "[LRS %p] Load reporting finished (lb_id_: %s, handler: %p).",
            service_, lb_id_.c_str(), this);
  }
  // TODO(juanlishen): This dummy shared_ptr is to keep a use count of the
  // handler until the method returns so that the function object can release
  // its ownership safely.
  auto dummy = shared_from_this();
  *func = nullptr;
}

}  // namespace load_reporter
}  // namespace grpc
