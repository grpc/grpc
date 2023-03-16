add_library(telemetry_interceptor TelemetryInterceptor.cpp TelemetryInterceptorFactory.cpp)
target_link_libraries(telemetry_interceptor ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})