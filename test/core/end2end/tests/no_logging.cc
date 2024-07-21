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

#include <atomic>
#include <map>
#include <regex>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

class VerifyLogNoiseLogSink : public absl::LogSink {
 public:
  explicit VerifyLogNoiseLogSink(const absl::LogSeverityAtLeast severity,
                                 const int verbosity) {
    saved_absl_severity_ = absl::MinLogLevel();
    absl::SetMinLogLevel(severity);
    // SetGlobalVLogLevel sets verbosity and returns previous verbosity.
    saved_absl_verbosity_ = absl::SetGlobalVLogLevel(verbosity);
    grpc_tracer_set_enabled("all", false);
    absl::AddLogSink(this);
  }

  ~VerifyLogNoiseLogSink() override {
    //  Reverse everything done in the constructor.
    absl::RemoveLogSink(this);
    saved_trace_flags_.Restore();
    absl::SetGlobalVLogLevel(saved_absl_verbosity_);
    absl::SetMinLogLevel(saved_absl_severity_);
  }

  // This function is called each time LOG or VLOG is called.
  void Send(const absl::LogEntry& entry) override {
    if (entry.log_severity() > absl::LogSeverity::kInfo ||
        entry.verbosity() < 1) {
      // For LOG(INFO) severity is INFO and verbosity is 0.
      // For VLOG(n) severity is INFO and verbosity is n.
      // LOG(INFO) and VLOG(0) have identical severity and verbosity.
      // We check log noise for LOG(INFO), LOG(WARNING) and LOG(ERROR).
      // We ignore VLOG(n) if (n>0) because we dont expect (n>0) in
      // production systems.
      CheckForNoisyLogs(entry);
    } else if (entry.log_severity() == absl::LogSeverity::kInfo &&
               entry.verbosity() >= 1) {
      std::cout << "\nno_logging_tracer " << entry.source_filename() << ":"
                << entry.source_line() << "\n";
    }
  }

  VerifyLogNoiseLogSink(const VerifyLogNoiseLogSink& other) = delete;
  VerifyLogNoiseLogSink& operator=(const VerifyLogNoiseLogSink& other) = delete;

 private:
  void CheckForNoisyLogs(const absl::LogEntry& entry) {
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
    CHECK(false) << "Unwanted log: Either change to VLOG(2) or add it to "
                    "allowed_logs_by_module "
                 << entry.source_filename() << ":" << entry.source_line() << " "
                 << entry.text_message();
  }
  absl::LogSeverityAtLeast saved_absl_severity_;
  int saved_absl_verbosity_;
  SavedTraceFlags saved_trace_flags_;
};

void SimpleRequest(CoreEnd2endTest& test) {
  VLOG(1) << "If the test fails here, the test is broken because it is not "
             "able to differentiate between LOG(INFO) and VLOG(1)";

  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
}

CORE_END2END_TEST(NoLoggingTest, NoLoggingTest) {
// This test makes sure that we don't get log noise when making an rpc
// especially when rpcs are successful.

// TODO(hork): remove when the listener flake is identified
#ifdef GPR_WINDOWS
  if (IsEventEngineListenerEnabled()) {
    GTEST_SKIP() << "not for windows + event engine listener";
  }
#endif
  VerifyLogNoiseLogSink nolog_verifier(absl::LogSeverityAtLeast::kInfo, 2);
  for (int i = 0; i < 10; i++) {
    SimpleRequest(*this);
  }
}
}  // namespace grpc_core
