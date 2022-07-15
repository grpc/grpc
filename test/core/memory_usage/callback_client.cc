#include <stdio.h>
#include <string.h>

#include <map>

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

class ClientCallbackImpl {
 public:
  ClientCallbackImpl(
      std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub)
      : stub_(std::move(stub)) {}

  void UnaryCall(std::function<void()> on_done) {
    struct CallParams {
      grpc::ClientContext context;
      grpc::testing::SimpleRequest request;
      grpc::testing::SimpleResponse response;
    };

    CallParams* params = new CallParams();

    auto callback = [params, on_done](const grpc::Status& status) {
      if (!status.ok()) {
        LOG(ERROR) << "UnaryCall RPC failed.";
        delete params;
        LOG(INFO) << "Before on done";
        on_done();
        LOG(INFO) << "After on done";
        return;
      }
      LOG(INFO) << "UnaryCall RPC succeeded.";
      delete params;
      // on_done();
      return;
    };

    // Start a call.
    stub_->async()->UnaryCall(&params->context, &params->request,
                              &params->response, callback);
  }

 private:
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub_;
};

ABSL_FLAG(std::string, target, "localhost:443", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

int main(int argc, char** argv) {
  // testing::InitGoogleTest(&argc, argv);
  // LOG(INFO)<< absl::GetFlag(FLAGS_target);
  absl::ParseCommandLine(argc, argv);
  LOG(INFO) << absl::GetFlag(FLAGS_target);
  // grpc_slice slice = grpc_slice_from_copied_string("x");
  char* fake_argv[1];

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);

  //grpc_init();
  LOG(INFO) << "After Client Init";

  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    printf("Supposed to be secure\n");
    // creds = Loas2Credentials(grpc::SslCredentialsOptions());
  }

  // Use gRPC calls to seek for or suggest fortune.
  std::shared_ptr<grpc::Channel> channel =
      CreateChannel(absl::GetFlag(FLAGS_target), creds);
  ClientCallbackImpl client(grpc::testing::BenchmarkService::NewStub(channel));

  client.UnaryCall([]() {
    LOG(INFO) << "ON DONE\n";
  });  // may need to add the on done parameter
  LOG(INFO) << "Client Done";
  return 0;
}
