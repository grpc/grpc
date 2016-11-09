#include "src/core/lib/tunnel/tunnel.h"

#include <string.h>
#include <grpc/impl/codegen/atm.h>
#include <grpc/support/alloc.h>
#include "src/core/lib/channel/channel_args.h"

void grpc_tunnel_start(grpc_tunnel *tunnel) {
  tunnel->vtable->start(tunnel);
}

void grpc_tunnel_shutdown(grpc_tunnel *tunnel) {
  tunnel->vtable->shutdown(tunnel);
}

void grpc_destroy_tunnel(grpc_tunnel *tunnel){
  tunnel->vtable->destroy(tunnel);
  grpc_channel_args_destroy(tunnel->tunnel_args);
  gpr_free((void *)tunnel);
}

grpc_channel* grpc_tunnel_channel_create(
    const char* target, const grpc_channel_args *args, void *reserved,
    grpc_tunnel *tunnel) {
  return tunnel->vtable->tunnel_channel_create(target, args, reserved, tunnel);
}

int grpc_server_add_tunnel(
    grpc_server *server, const char *addr, grpc_tunnel *tunnel) {
  return tunnel->vtable->server_add_tunnel(server, addr, tunnel);
}

void tunnel_internal_init(grpc_tunnel* tunnel,
                          grpc_channel_args *tunnel_args,
                          const grpc_tunnel_vtable *vtable) {
  tunnel->vtable = vtable;
  tunnel->tunnel_args = tunnel_args;
  gpr_atm_no_barrier_store(&tunnel->next_tag, 1);
}

void* tunnel_get_next_tag(grpc_tunnel* tunnel) {
  return (void *) gpr_atm_no_barrier_fetch_add(&tunnel->next_tag, 1);
}

/** This ought to be part of grpc_channel_args.h */
static int grpc_channel_arg_get_int_value(
    const grpc_channel_args *a, const char *key, int default_value) {
  size_t i;
  if (a == NULL) return default_value;
  for (i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, key)) {
      return a->args[i].value.integer;
    }
  }
  return default_value;
}

/** This probably needs to be part of grpc_completion_queue.h */
void completion_queue_drain(grpc_tunnel* tunnel,
                            grpc_completion_queue* cq) {
  gpr_timespec tunnel_shutdown_timeout = tunnel_get_shutdown_timeout(tunnel);
  grpc_completion_queue_shutdown(cq);
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, tunnel_shutdown_timeout, NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);
}


gpr_timespec tunnel_get_shutdown_timeout(grpc_tunnel* tunnel) {
  int timeout_ms = grpc_channel_arg_get_int_value(
      tunnel->tunnel_args, GRPC_ARG_TUNNEL_SHUTDOWN_TIMEOUT_MS,
      TUNNEL_DEFAULT_SHUTDOWN_TIMEOUT_MS);
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_micros((int64_t)(1e3 * (timeout_ms)),
                                           GPR_TIMESPAN));
}
