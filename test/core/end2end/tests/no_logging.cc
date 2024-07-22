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
                                 const int verbosity)
      : no_unwanted_logs_(true) {
    saved_absl_severity_ = absl::MinLogLevel();
    absl::SetMinLogLevel(severity);
    // SetGlobalVLogLevel sets verbosity and returns previous verbosity.
    saved_absl_verbosity_ = absl::SetGlobalVLogLevel(verbosity);
    grpc_tracer_set_enabled("all", false);
    absl::AddLogSink(this);
  }

  ~VerifyLogNoiseLogSink() override {
    CHECK(no_unwanted_logs_);
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

    // This list is an allow list of all VLOG(n) and DVLOG(n) statements. Right
    // now we dont have a way to differentiate between VLOG and DVLOG using
    // LogEntry. We can allow this list to grow because we dont expect VLOG to
    // be on in production systems.
    static const auto* const allowed_vlogs_by_module =
        new std::map<absl::string_view, std::regex>({
            {"dns_resolver_plugin.cc", std::regex("Using .* dns resolver")},
            {"dual_ref_counted.h", std::regex(".*")},
            {"http_connect_handshaker.cc",
             std::regex("Connecting to server.*")},
            {"http_proxy_fixture.cc",
             std::regex(
                 ".*")},  // "on_read_request_done:" and "Proxy address: .*"
            {"lb_policy_registry.cc",
             std::regex("registering LB policy factory for \".*\"")},
            {"no_logging.cc",
             std::regex("If the test fails here, the test is broken.*")},
            {"posix_endpoint.cc", std::regex("cannot set inq fd=.*")},
            {"posix_engine_listener_utils.cc", std::regex(".*")},
            {"proxy.cc", std::regex("")},
            {"ssl_security_connector.cc", std::regex(".*")},
            {"tcp_posix.cc", std::regex(".*")},
            {"tcp_server_utils_posix_common.cc",
             std::regex("Node does not support.*")},
        });

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
    if (IsVlogWithVerbosityMoreThan1(entry)) {
      // For LOG(INFO) severity is INFO and verbosity is 0.
      // For VLOG(n) severity is INFO and verbosity is n.
      // LOG(INFO) and VLOG(0) have identical severity and verbosity.
      auto it = allowed_vlogs_by_module->find(filename);
      if (it != allowed_vlogs_by_module->end() &&
          std::regex_search(std::string(entry.text_message()), it->second)) {
        return;
      }
    }
    no_unwanted_logs_ = false;
    LOG(ERROR)
        << "Unwanted log: Either user a tracer (example GRPC_TRACE_LOG or "
           "GRPC_TRACE_VLOG) or add it to allowed_logs_by_module or "
           "allowed_vlogs_by_module. Location : "
        << entry.source_filename() << ":" << entry.source_line() << " "
        << entry.text_message();
  }
  absl::LogSeverityAtLeast saved_absl_severity_;
  int saved_absl_verbosity_;
  SavedTraceFlags saved_trace_flags_;
  bool no_unwanted_logs_;
};

void SimpleRequest(CoreEnd2endTest& test) {
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
