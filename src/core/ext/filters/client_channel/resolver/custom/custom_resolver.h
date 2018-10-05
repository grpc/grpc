#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_CUSTOM_CUSTOM_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_CUSTOM_CUSTOM_RESOLVER_H

#include <grpc/grpc.h>

namespace grpc_core {

/** Add the resolver \a result to \a base_args.
  NOTE: This method is exposed for testing purposes only. */
grpc_channel_args* add_resolver_result_to_channel_args(
    grpc_channel_args* base_args, const grpc_resolver_result* result);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_CUSTOM_CUSTOM_RESOLVER_H \
        */
