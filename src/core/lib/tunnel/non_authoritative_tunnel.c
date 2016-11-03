#include "src/core/lib/tunnel/tunnel.h"

#include <grpc/support/alloc.h>

typedef struct non_authoritative_tunnel {
  grpc_tunnel base;  // Must be the first element
  grpc_channel *channel;
  grpc_completion_queue *tunnel_queue;
} non_authoritative_tunnel;

static const grpc_tunnel_vtable vtable;

static void non_authoritative_tunnel_channel_endpoint_create(
    grpc_tunnel *tunnelp, grpc_exec_ctx *exec_ctx, grpc_closure *closure,
    grpc_endpoint **ep, grpc_pollset_set *interested_parties,
    const struct sockaddr *addr, size_t addr_len, gpr_timespec deadline) {
}

static grpc_channel* non_authoritative_tunnel_channel_create(
    const char *target, const grpc_channel_args *args, void *reserved,
    grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  return NULL;
}

static int server_add_non_authoritative_tunnel(
    grpc_server *server, const char *addr, grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);

  return GRPC_STATUS_UNAVAILABLE;
}

static void on_non_authoritative_tunnel_server_listener_start(
    tunnel_server_listener *listener) {

}


static void non_authoritative_tunnel_startup(grpc_tunnel *tunnel) {
}

static void non_authoritative_tunnel_shutdown(grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  non_authoritative_tunnel *na_tunnel = (non_authoritative_tunnel *) tunnel;
  completion_queue_drain(tunnel, na_tunnel->tunnel_queue);
}

static void non_authoritative_tunnel_destroy(grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  non_authoritative_tunnel *na_tunnel = (non_authoritative_tunnel *) tunnel;
  grpc_channel_destroy(na_tunnel->channel);
  grpc_completion_queue_destroy(na_tunnel->tunnel_queue);
}

static const grpc_tunnel_vtable vtable = {
    &non_authoritative_tunnel_channel_create,
    &non_authoritative_tunnel_channel_endpoint_create,
    &server_add_non_authoritative_tunnel,
    &on_non_authoritative_tunnel_server_listener_start,
    &non_authoritative_tunnel_startup,
    &non_authoritative_tunnel_shutdown,
    &non_authoritative_tunnel_destroy};

grpc_tunnel* grpc_non_authoritative_tunnel_create(
    grpc_channel *tunneling_channel,
    grpc_channel_args *tunnel_args,
    grpc_completion_queue *tunnel_queue) {
  non_authoritative_tunnel *tunnel = (non_authoritative_tunnel *) gpr_malloc(
      sizeof(non_authoritative_tunnel));
  tunnel_internal_init((grpc_tunnel *)tunnel, tunnel_args, &vtable);
  tunnel->channel = tunneling_channel;
  tunnel->tunnel_queue = tunnel_queue;
  return (grpc_tunnel *) tunnel;
}
