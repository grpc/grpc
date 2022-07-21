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

#include <stdio.h>
#include <string.h>
#include <fstream>

#include <gtest/gtest.h>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "util/logging.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/client_callback.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(int, server_pid, 99999, "Server's pid");

std::unique_ptr<grpc::testing::BenchmarkService::Stub> CreateStubForTest(){
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



long GetSnapshot(bool before) {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub = CreateStubForTest();

  // Start a call.
  struct CallParams {
    grpc::ClientContext context;
    grpc::testing::SimpleRequest request;
    grpc::testing::MemorySize response;
  };
  CallParams* params = new CallParams();

  if(before){
    stub->async()->GetBeforeSnapshot(&params->context, &params->request,
                           &params->response, [params](const grpc::Status& status) {
                             if (status.ok()) {
                                gpr_log(GPR_INFO, "Before: %ld", params->response.rss());
                                gpr_log(GPR_INFO, "GetBeforeSnapshot succeeded.");
                             } else {
                                gpr_log(GPR_ERROR, "GetBeforeSnapshot failed.");
                             }
                           });
  }
  else{
    stub->async()->GetPeakSnapshot(&params->context, &params->request,
                           &params->response, [params](const grpc::Status& status) {
                             if (status.ok()) {
                                gpr_log(GPR_INFO, "Peak: %ld", params->response.rss());
                                gpr_log(GPR_INFO, "GetAfterSnapshot succeeded.");
                             } else {
                                gpr_log(GPR_ERROR, "GetAfterSnapshot failed.");
                             }
                           });
  }
  return params->response.rss();
}

long PIDMemory() {
  //Add required include statements
  double vm_usage     = 0.0;
  double resident_set = 0.0;

  // 'file' stat seems to give the most reliable results
  //
  std::ifstream stat_stream(absl::StrCat("/proc/", absl::GetFlag(FLAGS_server_pid),"/stat"), std::ios_base::in);

  // dummy vars for leading entries in stat that we don't care about
  //
  std::string pid, comm, state, ppid, pgrp, session, tty_nr;
  std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  std::string utime, stime, cutime, cstime, priority, nice;
  std::string O, itrealvalue, starttime;

  // the two fields we want
  //
  unsigned long vsize;
  long rss;

  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
              >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
              >> utime >> stime >> cutime >> cstime >> priority >> nice
              >> O >> itrealvalue >> starttime >> vsize >> rss; // don't care about the rest

  stat_stream.close();

  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // in case x86-64 is configured to use 2MB pages
  vm_usage     = vsize / 1024.0;
  resident_set = rss * page_size_kb;
  return resident_set;
}

void UnaryCall() {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub = CreateStubForTest();

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
                               GetSnapshot(false);
                             } else {
                               gpr_log(GPR_ERROR, "UnaryCall RPC failed.");
                             }
                           });
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

  long method_attempt = PIDMemory();
  gpr_log(GPR_INFO, "Method: %ld", method_attempt);
  long before_server_create = GetSnapshot(true);
  UnaryCall();
  
  //gpr_log(GPR_INFO, "Before haha: %ld", before_server_create);
  //long after_server_usage = GetSnapshot(false);
  gpr_log(GPR_INFO, "Client Done");
  ///Thoughts: Mutex around a finished counter, maybe a while statements to wait until it reaches 100, and then ask for peak?
  //Issue right now: can't use size value in main cause it hasnt be changed yet
  //Does server receive them in order
  //make it synchoncous or use notifications? let test PID first
  return 0;
}
