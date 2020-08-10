#ifndef GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H
#define GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H

#include "src/core/lib/iomgr/endpoint.h"

grpc_endpoint* grpc_eval_args_mock_endpoint_create(const char* local_address,
                                                   const int local_port,
                                                   const char* peer_address,
                                                   const int peer_port);
void grpc_eval_args_mock_endpoint_destroy(grpc_endpoint* ep);

#endif  // GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H
