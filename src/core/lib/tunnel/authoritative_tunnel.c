#include "src/core/lib/tunnel/tunnel.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/lib/tunnel/tunnel_connector.h"

typedef struct tunnel_channel_request tunnel_channel_request;

typedef struct tunnel_channel_request {
  grpc_endpoint **ep;
  grpc_closure *closure;
  gpr_timespec deadline;
  void *tracking_tag;
  grpc_call *tunneling_call;
  grpc_call_details call_details;
  grpc_metadata_array request_initial_metadata;
  tunnel_channel_request *next;
  tunnel_channel_request *prev;
} tunnel_channel_request;


typedef struct authoritative_tunnel {
  grpc_tunnel base;  // Must be the first element
  grpc_server *server;
  grpc_completion_queue *tunnel_queue;
  gpr_mu channel_req_mu;
  tunnel_channel_request *channel_request_head;
  gpr_atm tunnel_thd_id;
  gpr_thd_options tunnel_thd_options;
  /** 0: New, 1: Running, 2: Stop Requested, 3: Stopped. */
  gpr_atm tunnel_thd_status;
} authoritative_tunnel;

static const grpc_tunnel_vtable vtable;

static gpr_atm authoritative_tunnel_thd_id = (gpr_atm)0;

static void authoritative_channel_request_processor(void *arg) {

}

static void authoritative_tunnel_create_channel_endpoint(grpc_tunnel *tunnelp,
    grpc_exec_ctx *exec_ctx, grpc_closure *closure, grpc_endpoint **ep,
    grpc_pollset_set *interested_parties, const struct sockaddr *addr,
    size_t addr_len, gpr_timespec deadline) {
  GPR_ASSERT(tunnelp->vtable == &vtable);
  authoritative_tunnel *a_tunnel = (authoritative_tunnel *) tunnelp;
  tunnel_channel_request *channel_req =
      (tunnel_channel_request *) gpr_malloc(sizeof(*channel_req));
  channel_req->ep = ep;
  channel_req->closure = closure;
  channel_req->deadline = deadline;
  channel_req->tracking_tag = tunnel_get_next_tag(tunnelp);
  grpc_metadata_array_init(&channel_req->request_initial_metadata);
  grpc_call_details_init(&channel_req->call_details);
  grpc_call_error error = grpc_server_request_call(
      a_tunnel->server, &channel_req->tunneling_call,
      &channel_req->call_details, &channel_req->request_initial_metadata,
      a_tunnel->tunnel_queue, a_tunnel->tunnel_queue,
      channel_req->tracking_tag);
  if (GRPC_CALL_OK != error) {
    gpr_free(channel_req);
    closure->error = GRPC_ERROR_CREATE(grpc_call_error_to_string(error));
    closure->cb(exec_ctx, closure->cb_arg, closure->error);
    return;
  }
  gpr_mu_lock(&a_tunnel->channel_req_mu);
  tunnel_channel_request * prev_head = a_tunnel->channel_request_head;
  channel_req->prev = NULL;
  channel_req->next = prev_head;
  if (NULL != prev_head) {
    prev_head->prev = channel_req;
  }
  a_tunnel->channel_request_head = channel_req;
  gpr_mu_unlock(&a_tunnel->channel_req_mu);
}

static grpc_channel* authoritative_tunnel_create_channel(
    const char *target, const grpc_channel_args *args, void *reserved,
    grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  return tunnel_channel_create(target, args, reserved, tunnel);
}

static int grpc_server_add_authoritative_tunnel(
    grpc_server *server, const char *addr, grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  // For future uses of grpc_tunnel
  gpr_log(GPR_ERROR, "grpc_server_add_authoritative_tunnel not implemented");
  return GRPC_STATUS_UNAVAILABLE;
}

static void on_authoritative_tunnel_server_listener_start(
    tunnel_server_listener *listener) {
  GPR_ASSERT(listener->tunnel->vtable == &vtable);
  // For future uses of grpc_tunnel
  gpr_log(GPR_ERROR,
          "on_authoritative_tunnel_server_listener_start not implemented");
}


static void authoritative_tunnel_start(grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  authoritative_tunnel *a_tunnel = (authoritative_tunnel *) tunnel;
  grpc_server_start(a_tunnel->server);
  gpr_atm_no_barrier_store(&a_tunnel->tunnel_thd_status, 1);
  a_tunnel->tunnel_thd_id =
      gpr_atm_no_barrier_fetch_add(&authoritative_tunnel_thd_id, 1);
  a_tunnel->tunnel_thd_options = gpr_thd_options_default();
  gpr_thd_new((gpr_thd_id*)&a_tunnel->tunnel_thd_id,
              &authoritative_channel_request_processor, a_tunnel,
              &a_tunnel->tunnel_thd_options);
}

static void authoritative_tunnel_shutdown(grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  authoritative_tunnel *a_tunnel = (authoritative_tunnel *) tunnel;
  gpr_atm_no_barrier_store(&a_tunnel->tunnel_thd_status, 2);
  void *tag = tunnel_get_next_tag(tunnel);
  grpc_server_shutdown_and_notify(a_tunnel->server, a_tunnel->tunnel_queue,
                                  tag);
  gpr_timespec tunnel_shutdown_timeout = tunnel_get_shutdown_timeout(tunnel);
  grpc_event cq_event = grpc_completion_queue_pluck(
      a_tunnel->tunnel_queue, tag, tunnel_shutdown_timeout, NULL);
  if (cq_event.type != GRPC_OP_COMPLETE){
    gpr_log_message(GPR_ERROR, "Unable to shutdown authoritative tunnel");
  }
  completion_queue_drain(tunnel, a_tunnel->tunnel_queue);
}

static void authoritative_tunnel_destroy(grpc_tunnel *tunnel) {
  GPR_ASSERT(tunnel->vtable == &vtable);
  authoritative_tunnel *a_tunnel = (authoritative_tunnel *) tunnel;
  grpc_server_destroy(a_tunnel->server);
  grpc_completion_queue_destroy(a_tunnel->tunnel_queue);
  gpr_mu_lock(&a_tunnel->channel_req_mu);
  tunnel_channel_request *head = a_tunnel->channel_request_head;
  tunnel_channel_request *to_remove;
  while (NULL != head) {
    to_remove = head;
    head = head->next;
    gpr_free(to_remove);
  }
  gpr_mu_unlock(&a_tunnel->channel_req_mu);
  gpr_mu_destroy(&a_tunnel->channel_req_mu);
}

static const grpc_tunnel_vtable vtable = {
    &authoritative_tunnel_create_channel,
    &authoritative_tunnel_create_channel_endpoint,
    &grpc_server_add_authoritative_tunnel,
    &on_authoritative_tunnel_server_listener_start,
    &authoritative_tunnel_start,
    &authoritative_tunnel_shutdown,
    &authoritative_tunnel_destroy};

grpc_tunnel* grpc_authoritative_tunnel_create(
    grpc_server *tunneling_server,
    grpc_channel_args *tunnel_args,
    grpc_completion_queue *tunnel_queue) {
  authoritative_tunnel *tunnel = (authoritative_tunnel *) gpr_malloc(
      sizeof(authoritative_tunnel));
  tunnel_internal_init((grpc_tunnel *)tunnel, tunnel_args, &vtable);
  tunnel->server = tunneling_server;
  tunnel->tunnel_queue = tunnel_queue;
  gpr_mu_init(&tunnel->channel_req_mu);
  tunnel->channel_request_head = NULL;
  gpr_atm_no_barrier_store(&tunnel->tunnel_thd_status, 0);
  return (grpc_tunnel *) tunnel;
}
