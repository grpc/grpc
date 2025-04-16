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

#include "src/cpp/server/load_reporter/load_reporter_async_service_impl.h"

#include <google/protobuf/repeated_ptr_field.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <grpcpp/support/status.h>
#include <inttypes.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/cpp/server/load_reporter/constants.h"

// IWYU pragma: no_include "google/protobuf/duration.pb.h"

namespace grpc {
namespace load_reporter {

void LoadReporterAsyncServiceImpl::CallableTag::Run(bool ok) {
  CHECK(handler_function_ != nullptr);
  CHECK_NE(handler_, nullptr);
  handler_function_(std::move(handler_), ok);
}

LoadReporterAsyncServiceImpl::LoadReporterAsyncServiceImpl(
    std::unique_ptr<ServerCompletionQueue> cq)
    : cq_(std::move(cq)) {
  thread_ =
      std::make_unique<grpc_core::Thread>("server_load_reporting", Work, this);
  std::unique_ptr<CpuStatsProvider> cpu_stats_provider = nullptr;
#if defined(GPR_LINUX) || defined(GPR_WINDOWS) || defined(GPR_APPLE)
  cpu_stats_provider = std::make_unique<CpuStatsProviderDefaultImpl>();
#endif
  load_reporter_ = std::make_unique<LoadReporter>(
      kFeedbackSampleWindowSeconds,
      std::unique_ptr<CensusViewProvider>(new CensusViewProviderDefaultImpl()),
      std::move(cpu_stats_provider));
}

LoadReporterAsyncServiceImpl::~LoadReporterAsyncServiceImpl() {
  // We will reach here after the server starts shutting down.
  shutdown_ = true;
  {
    grpc_core::MutexLock lock(&cq_shutdown_mu_);
    cq_->Shutdown();
  }
  if (next_fetch_and_sample_alarm_ != nullptr) {
    next_fetch_and_sample_alarm_->Cancel();
  }
  thread_->Join();
}

void LoadReporterAsyncServiceImpl::ScheduleNextFetchAndSample() {
  auto next_fetch_and_sample_time =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_millis(kFetchAndSampleIntervalSeconds * 1000,
                                        GPR_TIMESPAN));
  {
    grpc_core::MutexLock lock(&cq_shutdown_mu_);
    if (shutdown_) return;
    // TODO(juanlishen): Improve the Alarm implementation to reuse a single
    // instance for multiple events.
    next_fetch_and_sample_alarm_ = std::make_unique<Alarm>();
    next_fetch_and_sample_alarm_->Set(cq_.get(), next_fetch_and_sample_time,
                                      this);
  }
  VLOG(2) << "[LRS " << this << "] Next fetch-and-sample scheduled.";
}

void LoadReporterAsyncServiceImpl::FetchAndSample(bool ok) {
  if (!ok) {
    LOG(INFO) << "[LRS " << this << "] Fetch-and-sample is stopped.";
    return;
  }
  VLOG(2) << "[LRS " << this << "] Starting a fetch-and-sample...";
  load_reporter_->FetchAndSample();
  ScheduleNextFetchAndSample();
}

void LoadReporterAsyncServiceImpl::Work(void* arg) {
  LoadReporterAsyncServiceImpl* service =
      static_cast<LoadReporterAsyncServiceImpl*>(arg);
  service->FetchAndSample(true /* ok */);
  // TODO(juanlishen): This is a workaround to wait for the cq to be ready. Need
  // to figure out why cq is not ready after service starts.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  ReportLoadHandler::CreateAndStart(service->cq_.get(), service,
                                    service->load_reporter_.get());
  void* tag;
  bool ok;
  while (true) {
    if (!service->cq_->Next(&tag, &ok)) {
      // The completion queue is shutting down.
      CHECK(service->shutdown_);
      break;
    }
    if (tag == service) {
      service->FetchAndSample(ok);
    } else {
      auto* next_step = static_cast<CallableTag*>(tag);
      next_step->Run(ok);
    }
  }
}

void LoadReporterAsyncServiceImpl::StartThread() { thread_->Start(); }

void LoadReporterAsyncServiceImpl::ReportLoadHandler::CreateAndStart(
    ServerCompletionQueue* cq, LoadReporterAsyncServiceImpl* service,
    LoadReporter* load_reporter) {
  std::shared_ptr<ReportLoadHandler> handler =
      std::make_shared<ReportLoadHandler>(cq, service, load_reporter);
  ReportLoadHandler* p = handler.get();
  {
    grpc_core::MutexLock lock(&service->cq_shutdown_mu_);
    if (service->shutdown_) return;
    p->on_done_notified_ =
        CallableTag(std::bind(&ReportLoadHandler::OnDoneNotified, p,
                              std::placeholders::_1, std::placeholders::_2),
                    handler);
    p->next_inbound_ =
        CallableTag(std::bind(&ReportLoadHandler::OnRequestDelivered, p,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(handler));
    p->ctx_.AsyncNotifyWhenDone(&p->on_done_notified_);
    service->RequestReportLoad(&p->ctx_, &p->stream_, cq, cq,
                               &p->next_inbound_);
  }
}

LoadReporterAsyncServiceImpl::ReportLoadHandler::ReportLoadHandler(
    ServerCompletionQueue* cq, LoadReporterAsyncServiceImpl* service,
    LoadReporter* load_reporter)
    : cq_(cq),
      service_(service),
      load_reporter_(load_reporter),
      stream_(&ctx_),
      call_status_(WAITING_FOR_DELIVERY) {}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnRequestDelivered(
    std::shared_ptr<ReportLoadHandler> self, bool ok) {
  if (ok) {
    call_status_ = DELIVERED;
  } else {
    // AsyncNotifyWhenDone() needs to be called before the call starts, but the
    // tag will not pop out if the call never starts (
    // https://github.com/grpc/grpc/issues/10136). So we need to manually
    // release the ownership of the handler in this case.
    CHECK_NE(on_done_notified_.ReleaseHandler(), nullptr);
  }
  if (!ok || shutdown_) {
    // The value of ok being false means that the server is shutting down.
    Shutdown(std::move(self), "OnRequestDelivered");
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  CreateAndStart(cq_, service_, load_reporter_);
  {
    grpc_core::ReleasableMutexLock lock(&service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.Release();
      Shutdown(std::move(self), "OnRequestDelivered");
      return;
    }
    next_inbound_ =
        CallableTag(std::bind(&ReportLoadHandler::OnReadDone, this,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    stream_.Read(&request_, &next_inbound_);
  }
  // LB ID is unique for each load reporting stream.
  lb_id_ = load_reporter_->GenerateLbId();
  LOG(INFO) << "[LRS " << service_
            << "] Call request delivered (lb_id_: " << lb_id_
            << ", handler: " << this
            << "). Start reading the initial request...";
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnReadDone(
    std::shared_ptr<ReportLoadHandler> self, bool ok) {
  if (!ok || shutdown_) {
    if (!ok && call_status_ < INITIAL_REQUEST_RECEIVED) {
      // The client may have half-closed the stream or the stream is broken.
      LOG(INFO) << "[LRS " << service_
                << "] Failed reading the initial request from the stream "
                   "(lb_id_: "
                << lb_id_ << ", handler: " << this
                << ", done_notified: " << done_notified_
                << ", is_cancelled: " << is_cancelled_ << ").";
    }
    Shutdown(std::move(self), "OnReadDone");
    return;
  }
  // We only receive one request, which is the initial request.
  if (call_status_ < INITIAL_REQUEST_RECEIVED) {
    if (!request_.has_initial_request()) {
      Shutdown(std::move(self), "OnReadDone+initial_request_not_found");
    } else {
      call_status_ = INITIAL_REQUEST_RECEIVED;
      const auto& initial_request = request_.initial_request();
      load_balanced_hostname_ = initial_request.load_balanced_hostname();
      load_key_ = initial_request.load_key();
      load_reporter_->ReportStreamCreated(load_balanced_hostname_, lb_id_,
                                          load_key_);
      const auto& load_report_interval = initial_request.load_report_interval();
      load_report_interval_ms_ =
          static_cast<unsigned long>((load_report_interval.seconds() * 1000) +
                                     (load_report_interval.nanos() / 1000));
      LOG(INFO) << "[LRS " << service_
                << "] Initial request received. Start load reporting (load "
                   "balanced host: "
                << load_balanced_hostname_
                << ", interval: " << load_report_interval_ms_
                << " ms, lb_id_: " << lb_id_ << ", handler: " << this << ")...";
      SendReport(self, true /* ok */);
      // Expect this read to fail.
      {
        grpc_core::ReleasableMutexLock lock(&service_->cq_shutdown_mu_);
        if (service_->shutdown_) {
          lock.Release();
          Shutdown(std::move(self), "OnReadDone");
          return;
        }
        next_inbound_ =
            CallableTag(std::bind(&ReportLoadHandler::OnReadDone, this,
                                  std::placeholders::_1, std::placeholders::_2),
                        std::move(self));
        stream_.Read(&request_, &next_inbound_);
      }
    }
  } else {
    // Another request received! This violates the spec.
    LOG(ERROR) << "[LRS " << service_
               << "] Another request received (lb_id_: " << lb_id_
               << ", handler: " << this << ").";
    Shutdown(std::move(self), "OnReadDone+second_request");
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::ScheduleNextReport(
    std::shared_ptr<ReportLoadHandler> self, bool ok) {
  if (!ok || shutdown_) {
    Shutdown(std::move(self), "ScheduleNextReport");
    return;
  }
  auto next_report_time = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(load_report_interval_ms_, GPR_TIMESPAN));
  {
    grpc_core::ReleasableMutexLock lock(&service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.Release();
      Shutdown(std::move(self), "ScheduleNextReport");
      return;
    }
    next_outbound_ =
        CallableTag(std::bind(&ReportLoadHandler::SendReport, this,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    // TODO(juanlishen): Improve the Alarm implementation to reuse a single
    // instance for multiple events.
    next_report_alarm_ = std::make_unique<Alarm>();
    next_report_alarm_->Set(cq_, next_report_time, &next_outbound_);
  }
  VLOG(2) << "[LRS " << service_
          << "] Next load report scheduled (lb_id_: " << lb_id_
          << ", handler: " << this << ").";
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::SendReport(
    std::shared_ptr<ReportLoadHandler> self, bool ok) {
  if (!ok || shutdown_) {
    Shutdown(std::move(self), "SendReport");
    return;
  }
  grpc::lb::v1::LoadReportResponse response;
  auto loads = load_reporter_->GenerateLoads(load_balanced_hostname_, lb_id_);
  response.mutable_load()->Swap(&loads);
  auto feedback = load_reporter_->GenerateLoadBalancingFeedback();
  response.mutable_load_balancing_feedback()->Swap(&feedback);
  if (call_status_ < INITIAL_RESPONSE_SENT) {
    auto initial_response = response.mutable_initial_response();
    initial_response->set_load_balancer_id(lb_id_);
    initial_response->set_implementation_id(
        grpc::lb::v1::InitialLoadReportResponse::CPP);
    initial_response->set_server_version(kVersion);
    call_status_ = INITIAL_RESPONSE_SENT;
  }
  {
    grpc_core::ReleasableMutexLock lock(&service_->cq_shutdown_mu_);
    if (service_->shutdown_) {
      lock.Release();
      Shutdown(std::move(self), "SendReport");
      return;
    }
    next_outbound_ =
        CallableTag(std::bind(&ReportLoadHandler::ScheduleNextReport, this,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    stream_.Write(response, &next_outbound_);
    LOG(INFO) << "[LRS " << service_
              << "] Sending load report (lb_id_: " << lb_id_
              << ", handler: " << this
              << ", loads count: " << response.load().size() << ")...";
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnDoneNotified(
    std::shared_ptr<ReportLoadHandler> self, bool ok) {
  CHECK(ok);
  done_notified_ = true;
  if (ctx_.IsCancelled()) {
    is_cancelled_ = true;
  }
  LOG(INFO) << "[LRS " << service_
            << "] Load reporting call is notified done (handler: " << this
            << ", is_cancelled: " << is_cancelled_ << ").";
  Shutdown(std::move(self), "OnDoneNotified");
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Shutdown(
    std::shared_ptr<ReportLoadHandler> self, const char* reason) {
  if (!shutdown_) {
    LOG(INFO) << "[LRS " << service_
              << "] Shutting down the handler (lb_id_: " << lb_id_
              << ", handler: " << this << ", reason: " << reason << ").";
    shutdown_ = true;
    if (call_status_ >= INITIAL_REQUEST_RECEIVED) {
      load_reporter_->ReportStreamClosed(load_balanced_hostname_, lb_id_);
      next_report_alarm_->Cancel();
    }
  }
  // OnRequestDelivered() may be called after OnDoneNotified(), so we need to
  // try to Finish() every time we are in Shutdown().
  if (call_status_ >= DELIVERED && call_status_ < FINISH_CALLED) {
    grpc_core::MutexLock lock(&service_->cq_shutdown_mu_);
    if (!service_->shutdown_) {
      on_finish_done_ =
          CallableTag(std::bind(&ReportLoadHandler::OnFinishDone, this,
                                std::placeholders::_1, std::placeholders::_2),
                      std::move(self));
      // TODO(juanlishen): Maybe add a message proto for the client to
      // explicitly cancel the stream so that we can return OK status in such
      // cases.
      stream_.Finish(Status::CANCELLED, &on_finish_done_);
      call_status_ = FINISH_CALLED;
    }
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnFinishDone(
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    std::shared_ptr<ReportLoadHandler> /*self*/, bool ok) {
  if (ok) {
    LOG(INFO) << "[LRS " << service_
              << "] Load reporting finished (lb_id_: " << lb_id_
              << ", handler: " << this << ").";
  }
}

}  // namespace load_reporter
}  // namespace grpc
