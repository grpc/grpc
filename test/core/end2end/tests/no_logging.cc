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
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

void gpr_default_log(gpr_log_func_args* args);

namespace grpc_core {

class Verifier {
 public:
  Verifier() {
    if (gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
      saved_severity_ = GPR_LOG_SEVERITY_DEBUG;
    } else if (gpr_should_log(GPR_LOG_SEVERITY_INFO)) {
      saved_severity_ = GPR_LOG_SEVERITY_INFO;
    } else if (gpr_should_log(GPR_LOG_SEVERITY_ERROR)) {
      saved_severity_ = GPR_LOG_SEVERITY_ERROR;
    } else {
      saved_severity_ =
          static_cast<gpr_log_severity>(GPR_LOG_SEVERITY_ERROR + 1);
    }
    grpc_tracer_set_enabled("all", 0);
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    gpr_set_log_function(DispatchLog);
  }
  ~Verifier() {
    gpr_set_log_function(gpr_default_log);
    saved_trace_flags_.Restore();
    gpr_set_log_verbosity(saved_severity_);
  }
  Verifier(const Verifier&) = delete;
  Verifier& operator=(const Verifier&) = delete;

  void FailOnAnyLog() { g_log_func_.store(NoLog); }
  void FailOnNonErrorLog() { g_log_func_.store(NoErrorLog); }

 private:
  static void DispatchLog(gpr_log_func_args* args) { g_log_func_.load()(args); }

  static void NoLog(gpr_log_func_args* args) {
    static const auto* const allowed_logs_by_module =
        new std::map<absl::string_view, std::regex>(
            {{"cq_verifier.cc", std::regex("^Verify .* for [0-9]+ms")}});
    absl::string_view filename = args->file;
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
        std::regex_match(args->message, it->second)) {
      gpr_default_log(args);
      return;
    }
    std::string message = absl::StrCat("Unwanted log: ", args->message);
    args->message = message.c_str();
    gpr_default_log(args);
    GTEST_FAIL();
  }

  static void NoErrorLog(gpr_log_func_args* args) {
    if (args->severity == GPR_LOG_SEVERITY_ERROR) {
      NoLog(args);
    }
  }

  gpr_log_severity saved_severity_;
  SavedTraceFlags saved_trace_flags_;
  static std::atomic<gpr_log_func> g_log_func_;
};

std::atomic<gpr_log_func> Verifier::g_log_func_(gpr_default_log);

void SimpleRequest(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
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
  CoreEnd2endTest::IncomingCloseOnServer client_close;
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
// TODO(hork): remove when the listener flake is identified
#ifdef GPR_WINDOWS
  if (IsEventEngineListenerEnabled()) {
    return;
  }
#endif
  Verifier verifier;
  verifier.FailOnNonErrorLog();
  for (int i = 0; i < 10; i++) {
    SimpleRequest(*this);
  }
  verifier.FailOnAnyLog();
  SimpleRequest(*this);
}

}  // namespace grpc_core
