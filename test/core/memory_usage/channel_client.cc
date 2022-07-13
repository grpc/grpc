class ClientCallbackImpl {
 public:
    void UnaryCall(std::function<void()> on_done) {
        struct CallParams {
            grpc::ClientContext context;
            SimpleRequest request;
            SimpleResponse response;
        };

        CallParams* params = new CallParams();

        auto callback = [params, on_done](const grpc::Status& status) {
        if (!status.ok()) {
            LOG(ERROR) << "GetFeature RPC failed.";
            delete params;
            on_done();
            return;
        }
        LOG(INFO) << "GetFeature RPC succeeded.";
        delete params;
        on_done();
        };

        // Start a call.
        stub_->async()->GetFeature(&params->context,
                                    &params->request,
                                    &params->response, callback);
    }
};

ABSL_FLAG(std::string, target, "localhost:443", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

int main(int argc, char **argv) {
    InitGoogle(argv[0], &argc, &argv, true);

    // Set the authentication mechanism.
    std::shared_ptr<ChannelCredentials> creds = grpc_insecure_credentials_create();
    if(absl::GetFlag(FLAGS_secure))
    {
        creds = Loas2Credentials(grpc::Loas2CredentialsOptions());
    }

    // Use gRPC calls to seek for or suggest fortune.
    std::shared_ptr<Channel> channel =
        CreateChannel(absl::GetFlag(FLAGS_server), creds);
    ClientCallbackImpl client(BenchmarkService::NewStub(channel));

    client.UnaryCall(); //may need to add the on done parameter
    return 0;
}
