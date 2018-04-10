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

#include "src/cpp/server/load_reporter/load_reporter_async_service_impl.h"

namespace grpc {
namespace load_reporter {

LoadReporterAsyncServiceImpl::LoadReporterAsyncServiceImpl() {
#if defined(GPR_LINUX) || defined(GPR_WINDOWS)
  load_reporter_ = std::unique_ptr<LoadReporter>(new LoadReporter(
      kFeedbackSampleWindowSeconds,
      std::unique_ptr<CensusViewProvider>(new CensusViewProviderDefaultImpl()),
      std::unique_ptr<CpuStatsProvider>(new CpuStatsProviderDefaultImpl())));
#else
  load_reporter_ = std::unique_ptr<LoadReporter>(new LoadReporter(
      kFeedbackSampleWindowSeconds,
      std::unique_ptr<CensusViewProvider>(new CensusViewProviderDefaultImpl()),
      std::unique_ptr<CpuStatsProvider>(nullptr)));
#endif
  FetchAndSample(true);
}

LoadReporterAsyncServiceImpl* LoadReporterAsyncServiceImpl::GetInstance() {
  if (instance_ == nullptr) {
    instance_ = new LoadReporterAsyncServiceImpl();
  }
  return instance_;
}

void LoadReporterAsyncServiceImpl::ResetInstance() {
  if (instance_ != nullptr) {
    delete instance_;
  }
}

LoadReporterAsyncServiceImpl::~LoadReporterAsyncServiceImpl() {
  server_->Shutdown();
  // Always shut down the completion queue after the server.
  cq_->Shutdown();
}

void LoadReporterAsyncServiceImpl::ScheduleNextFetchAndSample() {
  auto next_step = new std::function<void(bool)>(
      std::bind(&LoadReporterAsyncServiceImpl::FetchAndSample, this,
                std::placeholders::_1));
  auto next_fetch_and_sample_time =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_millis(kFetchAndSampleIntervalSeconds * 1000,
                                        GPR_TIMESPAN));
  next_fetch_and_sample_alarm_.Set(cq_.get(), next_fetch_and_sample_time,
                                   next_step);
  gpr_log(GPR_DEBUG, "[LRS %p] Next fetch-and-sample scheduled.", this);
}

void LoadReporterAsyncServiceImpl::FetchAndSample(bool ok) {
  if (!ok) {
    gpr_log(GPR_INFO, "[LRS %p] Stop fetch-and-sample.", this);
    return;
  }
  gpr_log(GPR_DEBUG, "[LRS %p] Start a fetch-and-sample.", this);
  load_reporter_->FetchAndSample();
  ScheduleNextFetchAndSample();
}

void LoadReporterAsyncServiceImpl::Run() {
  // TODO(juanlishen): Which port to use?
  grpc::string server_address("0.0.0.0:50066");
  ServerBuilder server_builder;
  // TODO(juanlishen): Add security check.
  server_builder.AddListeningPort(server_address, InsecureServerCredentials());
  server_builder.RegisterService(&service_);
  cq_ = server_builder.AddCompletionQueue();
  server_ = server_builder.BuildAndStart();
  gpr_log(GPR_INFO, "[LRS %p] Load reporting server starts listening on %s",
          this, server_address.c_str());
  HandleRequests();
}

void LoadReporterAsyncServiceImpl::HandleRequests() {
  new ReportLoadHandler(cq_.get(), &service_, load_reporter_.get());
  void* tag;
  bool ok;
  while (true) {
    if (!cq_->Next(&tag, &ok)) {
      // The completion queue is shutting down.
      break;
    }
    auto next_step = static_cast<std::function<void(bool)>*>(tag);
    (*next_step)(ok);
    delete next_step;
  }
}

LoadReporterAsyncServiceImpl::ReportLoadHandler::ReportLoadHandler(
    ServerCompletionQueue* cq,
    grpc::lb::v1::LoadReporter::AsyncService* service,
    LoadReporter* load_reporter)
    : cq_(cq),
      service_(service),
      load_reporter_(load_reporter),
      stream_(&ctx_) {
  auto next_step = new std::function<void(bool)>(std::bind(
      &ReportLoadHandler::OnRequestDelivered, this, std::placeholders::_1));
  service->RequestReportLoad(&ctx_, &stream_, cq_, cq_, next_step);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnRequestDelivered(
    bool ok) {
  if (!ok) {
    // The value of ok is false means that the server is shutting down.
    Shutdown();
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every
  // handler instance will deallocate itself when it's done.
  new ReportLoadHandler(cq_, service_, load_reporter_);
  // LB ID is unique for each load reporting stream.
  lb_id_ = load_reporter_->GenerateLbId();
  gpr_log(GPR_INFO,
          "[LRS %p] Load report request delivered (lb_id_: %s, handler: %p). "
          "Start reading request.",
          LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(), this);
  auto next_step = new std::function<void(bool)>(
      std::bind(&ReportLoadHandler::OnReadDone, this, std::placeholders::_1));
  stream_.Read(&request_, next_step);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnReadDone(bool ok) {
  // We only receive one request, which is the initial request.
  if (!initial_request_received_) {
    if (ok) {
      if (!request_.has_initial_request()) {
        Shutdown();
      } else {
        const auto& initial_request = request_.initial_request();
        load_balanced_hostname_ = initial_request.load_balanced_hostname();
        load_key_ = initial_request.load_key();
        load_reporter_->ReportStreamCreated(load_balanced_hostname_, lb_id_,
                                            load_key_);
        const auto& load_report_interval =
            initial_request.load_report_interval();
        load_report_interval_ms_ =
            static_cast<uint64_t>(load_report_interval.seconds() * 1000 +
                                  load_report_interval.nanos() / 1000);
        initial_request_received_ = true;
        gpr_log(GPR_INFO,
                "[LRS %p] Initial request received. Start load reporting (load "
                "balanced host: %s, interval: %lu ms, lb_id_: %s, handler: %p)",
                LoadReporterAsyncServiceImpl::GetInstance(),
                load_balanced_hostname_.c_str(), load_report_interval_ms_,
                lb_id_.c_str(), this);
        SendReport(true /* ok */);
        auto next_step = new std::function<void(bool)>(std::bind(
            &ReportLoadHandler::OnReadDone, this, std::placeholders::_1));
        // Expect this read to fail
        stream_.Read(&subsequent_request_, next_step);
      }
    } else {
      // The client may have half-closed the stream or the stream is broken,
      // even before sending out the initial request.
      Shutdown();
    }
  } else {
    if (ok) {
      // Another request received! This violates the spec.
      gpr_log(GPR_ERROR,
              "[LRS %p] Another request received (lb_id_: %s, handler: %p).",
              LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(),
              this);
    } else {
      // TODO(juanlishen): Use AsyncNotifyOnDone to distinguish.
      // The client may have half-closed the stream or the stream is broken.
      gpr_log(GPR_INFO,
              "[LRS %p] Stream was half-closed by the client or it's broken "
              "(lb_id_: %s, handler: %p).",
              LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(),
              this);
    }
    Shutdown();
  }
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::ScheduleNextReport(
    bool ok) {
  if (shutdown_) return;
  if (!ok) {
    Shutdown();
    return;
  }
  auto next_step = new std::function<void(bool)>(
      std::bind(&ReportLoadHandler::SendReport, this, std::placeholders::_1));
  auto next_report_time = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(load_report_interval_ms_, GPR_TIMESPAN));
  next_report_alarm_.Set(cq_, next_report_time, next_step);
  // TODO(juanlishen): It's possible that this function (1) checks shutdown_
  // before Shutdown() sets that flag and (2) sets the alarm after Shutdown()
  // cancels the alarm. There might be cleaner way to fix this than
  // checking shutdown_ again and canceling the alarm immediately.
  if (shutdown_) {
    next_report_alarm_.Cancel();
    return;
  }
  gpr_log(GPR_DEBUG,
          "[LRS %p] Next load report scheduled (lb_id_: %s, handler: %p).",
          LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(), this);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::SendReport(bool ok) {
  if (shutdown_) return;
  if (!ok) {
    Shutdown();
    return;
  }
  ::grpc::lb::v1::LoadReportResponse response;
  auto loads = load_reporter_->GenerateLoads(load_balanced_hostname_, lb_id_);
  response.mutable_load()->Swap(&loads);
  auto feedback = load_reporter_->GenerateLoadBalancingFeedback();
  response.mutable_load_balancing_feedback()->Swap(&feedback);
  if (!initial_response_sent_) {
    auto initial_response = response.mutable_initial_response();
    initial_response->set_load_balancer_id(lb_id_);
    initial_response->set_implementation_id(
        ::grpc::lb::v1::InitialLoadReportResponse::CPP);
    initial_response->set_server_version(kVersion);
    initial_response_sent_ = true;
  }
  auto next_step = new std::function<void(bool)>(std::bind(
      &ReportLoadHandler::ScheduleNextReport, this, std::placeholders::_1));
  gpr_log(GPR_INFO, "[LRS %p] Send load report (lb_id_: %s, handler: %p).",
          LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(), this);
  stream_.Write(response, next_step);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::Shutdown() {
  gpr_log(GPR_INFO,
          "[LRS %p] Shutting down the handler (lb_id_: %s, handler: %p).",
          LoadReporterAsyncServiceImpl::GetInstance(), lb_id_.c_str(), this);
  shutdown_ = true;
  if (initial_request_received_) {
    load_reporter_->ReportStreamClosed(load_balanced_hostname_, lb_id_);
  }
  next_report_alarm_.Cancel();
  auto next_step = new std::function<void(bool)>(
      std::bind(&ReportLoadHandler::OnFinishDone, this, std::placeholders::_1));
  stream_.Finish(final_status_, next_step);
}

void LoadReporterAsyncServiceImpl::ReportLoadHandler::OnFinishDone(bool ok) {
  if (ok) {
    gpr_log(GPR_INFO,
            "[LRS %p] Load reporting finished (status ode: %d, status detail: "
            "%s, lb_id_: %s, handler: %p).",
            LoadReporterAsyncServiceImpl::GetInstance(),
            final_status_.error_code(), final_status_.error_details().c_str(),
            lb_id_.c_str(), this);
  }
  delete this;
}

}  // namespace load_reporter
}  // namespace grpc
