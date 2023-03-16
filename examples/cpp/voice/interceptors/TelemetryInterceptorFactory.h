#include <grpcpp/support/client_interceptor.h>

class TelemetryInterceptorFactory : public grpc::experimental::ClientInterceptorFactoryInterface
{
public:
    grpc::experimental::Interceptor* CreateClientInterceptor(grpc::experimental::ClientRpcInfo* info) override;
};