class PerChannelClient {
 public:
    void CallFunction() {
        int done_count = 0;
        GetOneFeature(409146138, -746188906, [&mu, &cv, &done_count]() {
        absl::MutexLock lock(&mu);
        ++done_count;
        cv.Signal();
        });
        GetOneFeature(
            0, 0,
            [&mu, &cv, &done_count]() {
            absl::MutexLock lock(&mu);
            ++done_count;
            cv.Signal();
            },
            /*server_debug=*/true);
        absl::MutexLock lock(&mu);
        while (done_count != 2) {
        cv.Wait(&mu);
        }
    }

    void UnaryCall(std::function<void()> on_done) {
        struct CallParams {
            grpc::ClientContext context;
            SimpleRequest request;
            SimpleResponse response;
        };

        CallParams* params = new CallParams();
        params->request.set_payloadType(COMPRESSABLE);


        auto callback = [params, on_done](const grpc::Status& status) {
        if (!status.ok()) {
            LOG(ERROR) << "GetFeature RPC failed.";
            delete feature_params;
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
