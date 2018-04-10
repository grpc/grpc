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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_REPORT_LOAD_HANDLER_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_REPORT_LOAD_HANDLER_H

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>

#include "src/cpp/server/load_reporter/load_reporter.h"

constexpr uint32_t kFeedbackSampleWindowSeconds = 10;
constexpr uint32_t kFetchAndSampleIntervalSeconds = 1;
// TODO(juanlishen): Update the version number with the PR number every time
// there is any change to the server load reporter.
constexpr uint32_t kVersion = 14793;

namespace grpc {
namespace load_reporter {

// A singleton class of async load reporting service. It's mainly responsible
// for controlling the procedure of incoming requests. The real business
// logic is handed off to the LoadReporter.
class LoadReporterAsyncServiceImpl {
 public:
  ~LoadReporterAsyncServiceImpl();

  // Gets the singleton instance.
  static LoadReporterAsyncServiceImpl* GetInstance();

  // Mirror method of GetInstance().
  static void ResetInstance();

  // Builds up the server and starts handling incoming load reporting requests.
  void Run();

  // Not copyable nor movable.
  LoadReporterAsyncServiceImpl(const LoadReporterAsyncServiceImpl&) = delete;
  LoadReporterAsyncServiceImpl& operator=(const LoadReporterAsyncServiceImpl&) =
      delete;

 private:
  // Each handler takes care of one load reporting stream. It contains
  // per-stream data and it will access the members of the parent class (i.e.,
  // LoadReporterAsyncServiceImpl) for service-wide data (e.g., the load data).
  class ReportLoadHandler {
   public:
    // When a new handler is constructed, it starts waiting for the next
    // load reporting request.
    ReportLoadHandler(ServerCompletionQueue* cq,
                      grpc::lb::v1::LoadReporter::AsyncService* service,
                      LoadReporter* load_reporter);

    // After a request is delivered to this handler, it starts reading the
    // request. Also, a new handler is spawned so that we can keep servicing
    // future requests.
    void OnRequestDelivered(bool ok);

    // The first Read() is expected to succeed, and then the handler starts
    // sending load reports back to the balancer. The second Read() is
    // expected to fail, which happens when the balancer half-closes the
    // stream to signal that it's no longer interested in the load reports.
    // The handler will then close the stream.
    void OnReadDone(bool ok);

    // The report sending operations are sequential as: send report -> send
    // done, schedule the next send -> waiting for the alarm to fire -> alarm
    // fires, send report -> ...
    void SendReport(bool ok);
    void ScheduleNextReport(bool ok);

    // When Finish() is done, the following function will destruct the handler.
    void OnFinishDone(bool ok);

   private:
    void Shutdown();
    // The data for RPC communication with the load reportee.
    ServerContext ctx_;
    ::grpc::lb::v1::LoadReportRequest request_;
    // The members passed down from LoadReporterAsyncServiceImpl.
    ServerCompletionQueue* cq_;
    grpc::lb::v1::LoadReporter::AsyncService* service_;
    LoadReporter* load_reporter_;
    // Only one request is expected, so other request will never be answered
    // even if it is received.
    ::grpc::lb::v1::LoadReportRequest subsequent_request_;
    ServerAsyncReaderWriter<::grpc::lb::v1::LoadReportResponse,
                            ::grpc::lb::v1::LoadReportRequest>
        stream_;
    // The status of the RPC progress.
    std::atomic_bool shutdown_{false};
    std::atomic_bool initial_request_received_{false};
    std::atomic_bool initial_response_sent_{false};
    Alarm next_report_alarm_;
    Status final_status_;
    // The metadata of the stream.
    grpc::string lb_id_;
    grpc::string load_balanced_hostname_;
    grpc::string load_key_;
    uint64_t load_report_interval_ms_;
  };

  LoadReporterAsyncServiceImpl();

  // Starts handling requests and drives the completion queue in a loop.
  void HandleRequests();

  // Schedules next data fetching from Census.
  void ScheduleNextFetchAndSample();

  // Fetches data from Census.
  void FetchAndSample(bool ok);

  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<Server> server_;
  grpc::lb::v1::LoadReporter::AsyncService service_;
  std::unique_ptr<LoadReporter> load_reporter_;

  Alarm next_fetch_and_sample_alarm_;
  // This has to be a raw pointer to be trivially destructible.
  static LoadReporterAsyncServiceImpl* instance_;
};

LoadReporterAsyncServiceImpl* LoadReporterAsyncServiceImpl::instance_ = nullptr;

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_REPORT_LOAD_HANDLER_H
