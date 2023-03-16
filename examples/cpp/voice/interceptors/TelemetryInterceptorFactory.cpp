#include "TelemetryInterceptorFactory.h"
#include "TelemetryInterceptor.h"
#include <iostream>

grpc::experimental::Interceptor* TelemetryInterceptorFactory::CreateClientInterceptor(grpc::experimental::ClientRpcInfo* info)
{
    std::cout << "!!!!!Factory!!!!!" << std::endl;
    return new TelemetryInterceptor();
}