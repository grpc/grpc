
#include "src/core/lib/transport/buffer_pool.h"

struct grpc_buffer_pool_user {};

struct grpc_buffer_pool {};

void grpc_buffer_pool_acquire(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *pool,
                              size_t amount, grpc_closure *on_ready) {}
