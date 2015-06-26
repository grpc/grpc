#include <grpc/grpc.h>

int main() {
  grpc_completion_queue *cq1;
  grpc_completion_queue *cq2;
  grpc_server *server;

  grpc_init();
  cq1 = grpc_completion_queue_create();
  cq2 = grpc_completion_queue_create();
  server = grpc_server_create(NULL);
  grpc_server_register_completion_queue(server, cq1);
  grpc_server_add_http2_port(server, "[::]:0");
  grpc_server_register_completion_queue(server, cq2);
  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq2, NULL);
  grpc_completion_queue_next(cq2, gpr_inf_future);  /* cue queue hang */
  grpc_completion_queue_shutdown(cq1);
  grpc_completion_queue_shutdown(cq2);
  grpc_completion_queue_next(cq1, gpr_inf_future);
  grpc_completion_queue_next(cq2, gpr_inf_future);
  grpc_server_destroy(server);
  grpc_shutdown();
  return 0;
}
