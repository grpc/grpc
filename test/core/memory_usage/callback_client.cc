/*
 *
 * Copyright 2022 gRPC authors.
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

#include <memory>
#include <string>
#include <fstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/string_view.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(int, server_pid, 99999, "Server's pid");

std::unique_ptr<grpc::testing::BenchmarkService::Stub> CreateStubForTest() {
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    // TODO (chennancy) Add in secure credentials
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
  }

  // Create a channel to the server and a stub
  std::shared_ptr<grpc::Channel> channel =
      CreateChannel(absl::GetFlag(FLAGS_target), creds);
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      grpc::testing::BenchmarkService::NewStub(channel);
  return stub;
}

void UnaryCall() {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      CreateStubForTest();

  // Start a call.
  struct CallParams {
    grpc::ClientContext context;
    grpc::testing::SimpleRequest request;
    grpc::testing::SimpleResponse response;
  };
  CallParams* params = new CallParams();
  stub->async()->UnaryCall(&params->context, &params->request,
                           &params->response, [](const grpc::Status& status) {
                             if (status.ok()) {
                               gpr_log(GPR_INFO, "UnaryCall RPC succeeded.");
                             } else {
                               gpr_log(GPR_ERROR, "UnaryCall RPC failed.");
                             }
                           });
}

// Get memory usage of server's process before the server is made
long GetBeforeSnapshot() {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      CreateStubForTest();

  // Start a call.
  struct CallParams {
    grpc::ClientContext context;
    grpc::testing::SimpleRequest request;
    grpc::testing::MemorySize response;
  };
  CallParams* params = new CallParams();
  stub->async()->GetBeforeSnapshot(
      &params->context, &params->request, &params->response,
      [params](const grpc::Status& status) {
        if (status.ok()) {
          gpr_log(GPR_INFO, "Before: %ld", params->response.rss());
          gpr_log(GPR_INFO, "GetBeforeSnapshot succeeded.");
        } else {
          gpr_log(GPR_ERROR, "GetBeforeSnapshot failed.");
        }
      });
  return params->response.rss();
}

// Get current memory usage of server without having to send an RPC by using
// server's pid
long GetServerMemory() {
  double resident_set = 0.0;
  std::ifstream stat_stream(
      absl::StrCat("/proc/", absl::GetFlag(FLAGS_server_pid), "/stat"),
      std::ios_base::in);

  // Temporary variables for irrelevant leading entries in stats
  std::string pid, comm, state, ppid, pgrp, session, tty_nr;
  std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  std::string utime, stime, cutime, cstime, priority, nice;
  std::string O, itrealvalue, starttime, vsize;

  // Get rss to find memory usage
  long rss;
  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >>
      tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >>
      stime >> cutime >> cstime >> priority >> nice >> O >> itrealvalue >>
      starttime >> vsize >> rss;
  stat_stream.close();

  // Calculations in case x86-64 is configured to use 2MB pages
  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  resident_set = rss * page_size_kb;
  return resident_set;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  char* fake_argv[1];
  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);
  if (absl::GetFlag(FLAGS_target).empty()) {
    gpr_log(GPR_ERROR, "Client: No target port entered");
    return 1;
  }
  gpr_log(GPR_INFO, "Client Target: %s", absl::GetFlag(FLAGS_target).c_str());

  // Getting memory usage
  long before_server_memory = GetBeforeSnapshot();
  MemStats before_client_memory = MemStats::Snapshot();
  gpr_log(GPR_INFO, "Before Client Mem: %ld", before_client_memory.rss);
  UnaryCall();
  long peak_server_memory = GetServerMemory();
  MemStats peak_client_memory = MemStats::Snapshot();
  gpr_log(GPR_INFO, "Peak Client Mem: %ld", peak_client_memory.rss);
  gpr_log(GPR_INFO, "Peak Server Mem: %ld", GetServerMemory());
  gpr_log(GPR_INFO, "Client Done");
  return 0;
}
