//
//
// Copyright 2015 gRPC authors.
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

#include <memory>

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

class VerifyLogNoiseLogSink1 : public absl::LogSink {
 public:
  explicit VerifyLogNoiseLogSink(const absl::LogSeverityAtLeast severity,
                                 const int verbosity)
      : log_noise_absent_(true) {
    saved_absl_severity_ = absl::MinLogLevel();
    absl::SetMinLogLevel(severity);
    // SetGlobalVLogLevel sets verbosity and returns previous verbosity.
    saved_absl_verbosity_ = absl::SetGlobalVLogLevel(verbosity);
    grpc_tracer_set_enabled("all", false);
    absl::AddLogSink(this);
  }

  ~VerifyLogNoiseLogSink() override {
    CHECK(log_noise_absent_)
        << "Unwanted logs present. This will cause log noise. Either user a "
           "tracer (example GRPC_TRACE_LOG or GRPC_TRACE_VLOG) or convert the "
           "statement to VLOG(2).";
    //  Reverse everything done in the constructor.
    absl::RemoveLogSink(this);
    saved_trace_flags_.Restore();
    absl::SetGlobalVLogLevel(saved_absl_verbosity_);
    absl::SetMinLogLevel(saved_absl_severity_);
  }

  // This function is called each time LOG or VLOG is called.
  void Send(const absl::LogEntry& entry) override { CheckForNoisyLogs(entry); }

  VerifyLogNoiseLogSink(const VerifyLogNoiseLogSink& other) = delete;
  VerifyLogNoiseLogSink& operator=(const VerifyLogNoiseLogSink& other) = delete;

 private:
  bool IsVlogWithVerbosityMoreThan1(const absl::LogEntry& entry) const {
    return entry.log_severity() == absl::LogSeverity::kInfo &&
           entry.verbosity() >= 1;
  }

  void CheckForNoisyLogs(const absl::LogEntry& entry) {
    // TODO(tjagtap) : Add a hard upper limit on number of times each log should
    // appear. We can keep this number slightly higher to avoid our tests
    // becoming flaky. Right now all entries in this list get a free pass to log
    // infinitely - That may create log noise issues in the future.
    //
    // This list is an allow list of all LOG(INFO), LOG(WARNING), and LOG(ERROR)
    // logs which will appear. For now we have decided to allow these instances.
    // We should be very conservative while adding new entries to this list,
    // because this has potential to cause massive log noise. Several users are
    // using INFO log level setting for production.
    static const auto* const allowed_logs_by_module =
        new std::map<absl::string_view, std::regex>(
            {{"cq_verifier.cc", std::regex("^Verify .* for [0-9]+ms")},
             {"chttp2_transport.cc",
              std::regex("Sending goaway.*Channel Destroyed")},
             {"config.cc", std::regex("gRPC experiments.*")},
             {"chaotic_good_server.cc",
              std::regex("Failed to bind some addresses for.*")},
             {"log.cc",
              std::regex("Prefer WARNING or ERROR. However if you see this "
                         "message in a debug environmenmt or test environmenmt "
                         "it is safe to ignore this message.")}});

    if (IsVlogWithVerbosityMoreThan1(entry)) {
      return;
    }

    absl::string_view filename = entry.source_filename();
    auto slash = filename.rfind('/');
    if (slash != absl::string_view::npos) {
      filename = filename.substr(slash + 1);
    }
    slash = filename.rfind('\\');
    if (slash != absl::string_view::npos) {
      filename = filename.substr(slash + 1);
    }
    auto it = allowed_logs_by_module->find(filename);
    if (it != allowed_logs_by_module->end() &&
        std::regex_search(std::string(entry.text_message()), it->second)) {
      return;
    }

    // If we reach here means we have log noise. log_noise_absent_ will make the
    // test fail.
    log_noise_absent_ = false;
    LOG(ERROR) << "Unwanted log at location : " << entry.source_filename()
               << ":" << entry.source_line() << " " << entry.text_message();
  }

  absl::LogSeverityAtLeast saved_absl_severity_;
  int saved_absl_verbosity_;
  SavedTraceFlags saved_trace_flags_;
  bool log_noise_absent_;
};

// Client pings and server pongs. Repeat messages rounds before finishing.
void PingPongStreaming(CoreEnd2endTest& test, int num_messages) {
  auto request_slice = RandomSlice(20);
  auto response_slice = RandomSlice(15);
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingMetadata server_initial_md;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .RecvInitialMetadata(server_initial_md)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(100);
  test.Expect(100, true);
  test.Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(101).SendInitialMetadata({}).RecvCloseOnServer(client_close);
  for (int i = 0; i < num_messages; i++) {
    IncomingMessage server_message;
    c.NewBatch(2).SendMessage(request_slice.Ref()).RecvMessage(server_message);
    IncomingMessage client_message;
    s.NewBatch(102).RecvMessage(client_message);
    test.Expect(102, true);
    test.Step();
    s.NewBatch(103).SendMessage(response_slice.Ref());
    test.Expect(2, true);
    test.Expect(103, true);
    test.Step();
  }
  c.NewBatch(3).SendCloseFromClient();
  s.NewBatch(104).SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  test.Expect(1, true);
  test.Expect(3, true);
  test.Expect(101, true);
  test.Expect(104, true);
  test.Step();
}

CORE_END2END_TEST(CoreEnd2endTest, PingPongStreaming1) {
  PingPongStreaming(*this, 1);
}

CORE_END2END_TEST(CoreEnd2endTest, PingPongStreaming3) {
  PingPongStreaming(*this, 3);
}

CORE_END2END_TEST(CoreEnd2endTest, PingPongStreaming10) {
  PingPongStreaming(*this, 10);
}

CORE_END2END_TEST(CoreEnd2endTest, PingPongStreaming30) {
  PingPongStreaming(*this, 30);
}

CORE_END2END_TEST(CoreEnd2endTest, PingPongStreamingLogging) {
// This test makes sure that we don't get log noise when making an rpc
// especially when rpcs are successful.

// TODO(hork): remove when the listener flake is identified
#ifdef GPR_WINDOWS
  if (IsEventEngineListenerEnabled()) {
    GTEST_SKIP() << "not for windows + event engine listener";
  }
#endif
  VerifyLogNoiseLogSink1 nolog_verifier(absl::LogSeverityAtLeast::kInfo, 2);
  PingPongStreaming(*this, 30);
}

}  // namespace grpc_core
