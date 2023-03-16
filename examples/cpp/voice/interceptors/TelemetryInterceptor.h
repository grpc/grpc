#include <grpcpp/support/interceptor.h>

class TelemetryInterceptor : public grpc::experimental::Interceptor
{
public:
    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;
};