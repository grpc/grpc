#include "TelemetryInterceptor.h"
#include <iostream>

void TelemetryInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods)
{
    // Use telemetry instead of prints, and maybe a better way than this many if-elses
    std::cout << "----- Interception hook point: ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA))
        std::cout << "PRE_SEND_INITIAL_METADATA ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_MESSAGE))
        std::cout << "PRE_SEND_MESSAGE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_SEND_MESSAGE))
        std::cout << "POST_SEND_MESSAGE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS))
        std::cout << "PRE_SEND_STATUS ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_CLOSE))
        std::cout << "PRE_SEND_CLOSE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_RECV_INITIAL_METADATA))
        std::cout << "PRE_RECV_INITIAL_METADATA ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_RECV_MESSAGE))
        std::cout << "PRE_RECV_MESSAGE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_RECV_STATUS))
        std::cout << "PRE_RECV_STATUS ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA))
        std::cout << "POST_RECV_INITIAL_METADATA ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_RECV_MESSAGE))
        std::cout << "POST_RECV_MESSAGE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_RECV_STATUS))
        std::cout << "POST_RECV_STATUS ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::POST_RECV_CLOSE))
        std::cout << "POST_RECV_CLOSE ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::PRE_SEND_CANCEL))
        std::cout << "PRE_SEND_CANCEL ";
    if (methods->QueryInterceptionHookPoint(grpc::experimental::InterceptionHookPoints::NUM_INTERCEPTION_HOOKS))
        std::cout << "NUM_INTERCEPTION_HOOKS ";
    std::cout << "-----" << std::endl;

    methods->Proceed();
}