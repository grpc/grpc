#include "grpc/src/proto/grpc/testing/benchmark_service.proto"

class ServerCallbackImpl final
    : public grpc_gen::BenchmarkService::CallbackService {

    grpc::ServerUnaryReactor* UnaryCall(grpc::CallbackServerContext* context,
                                       const SimpleRequest* request,
                                       SimpleResponse* response) override {
    LOG(INFO) << "RPC CALL RECEIVED";
    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
    }
};

ABSL_FLAG(std::string, port, "", "Bind host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

int main(int argc, char** argv) {
    InitGoogle(argv[0], &argc, &argv, true);
    absl::SetFlag(&FLAGS_alsologtostderr, true);

    std::string server_address = absl::StrCat("[::]:", absl::GetFlag(FLAGS_port));

    ServerCallbackImpl callback_server;
    grpc::ServerBuilder builder;

    if(absl::GetFlag(FLAGS_secure))
    {
        // Set the authentication mechanism.
        std::shared_ptr<grpc::ServerCredentials> creds =
        grpc::Loas2ServerCredentials(grpc::Loas2ServerCredentialsOptions());
        // Listen on the given address with a secure server.
        builder.AddListeningPort(server_address, creds);
    }
    else{
        // Listen on the given address with a insecure server.
        builder.AddListeningPort(server_address, InsecureServerCredentials());
    }
    
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