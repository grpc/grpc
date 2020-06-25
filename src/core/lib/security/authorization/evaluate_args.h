//
// Created by dohyunc on 6/17/20.
//

#ifndef GRPC_EVALUATE_ARGS_H
#define GRPC_EVALUATE_ARGS_H

#include <src/core/lib/iomgr/endpoint.h>
#include <src/core/lib/security/context/security_context.h>
#include <src/core/lib/transport/metadata_batch.h>


//Holds grpc values that will be converted to the necessary Envoy args.
struct EvaluateArgs {
  const grpc_metadata_batch* metadata;
  const grpc_endpoint* endpoint;
  const grpc_auth_context* auth_context;
};




#endif  // GRPC_EVALUATE_ARGS_H

