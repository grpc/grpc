#include "grpc/src/proto/grpc/testing/benchmark_service.proto"

class PerChannelCallbackImpl final
    : public grpc_gen::BenchmarkService::CallbackService {

    grpc::ServerUnaryReactor* UnaryCall(grpc::CallbackServerContext* context,
                                       const SimpleRequest* request,
                                       SimpleResponse* response) override {
    response->payload->set_payloadType(request->response_type);
    std::string load = "Hello World";
    //ignoring response_size for now
    response->payload->set_body(load);

    auto* reactor = context->DefaultReactor();

    
    reactor->Finish(grpc::Status::OK);
    return reactor;
    }
};