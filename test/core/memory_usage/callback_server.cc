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

class ServerCallbackImpl final
    : public grpc::testing::BenchmarkService::CallbackService {

    grpc::ServerUnaryReactor* UnaryCall(grpc::CallbackServerContext* context,
                                       const grpc::testing::SimpleRequest* request,
                                       grpc::testing::SimpleResponse* response) override {
    LOG(INFO) << "RPC CALL RECEIVED";

    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
    }
};

ABSL_FLAG(std::string, bind, "", "Bind host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

int main(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);
    LOG(INFO) <<"Server Process Started";
    LOG(INFO)<<absl::GetFlag(FLAGS_bind);
    LOG(INFO)<<"IS THIS STILL WORKING";
    //grpc_slice slice = grpc_slice_from_copied_string("x");
    char* fake_argv[1];
    
    GPR_ASSERT(argc >= 1);
    fake_argv[0] = argv[0];
    grpc::testing::TestEnvironment env(&argc, argv);

    grpc_init();
    //absl::SetFlag(&FLAGS_alsologtostderr, true);
    LOG(INFO)<<"After Server Init";
    std::string server_address = absl::GetFlag(FLAGS_bind);
    LOG(INFO)<<server_address;
    
    LOG(INFO)<<"Before Listening port";


    ServerCallbackImpl callback_server;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    /*if(!absl::GetFlag(FLAGS_secure))
    {
        // Listen on the given address with a insecure server.
        
    }
    else{
        
        // Set the authentication mechanism.
        /*std::shared_ptr<grpc::ServerCredentials> creds =
        grpc::Loas2ServerCredentials(grpc::Loas2ServerCredentialsOptions());
        // Listen on the given address with a secure server.
        builder.AddListeningPort(server_address, creds);*
        printf("Supposed to be secure \n");
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    }*/
    LOG(INFO)<<"After Listening port";
    // Register "service" as the instance through which we'll communicate with
    // clients.
    builder.RegisterService(&callback_server);
    // Set up the server to start accepting requests.
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LOG(INFO) << "Server listening on " << server_address;

    // Keep the program running until the server shuts down.
    server->Wait();

    return 0;
}