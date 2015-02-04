#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include <string.h>

grpc_byte_buffer *string_to_byte_buffer(const char *buffer, size_t len) {
  gpr_slice slice = gpr_slice_from_copied_buffer(buffer, len);
  grpc_byte_buffer *bb = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  return bb;
}

void grpc_call_start_write_from_copied_buffer(grpc_call *call,
                                              const char *buffer, size_t len,
                                              void *tag, gpr_uint32 flags) {
  grpc_byte_buffer *byte_buffer = string_to_byte_buffer(buffer, len);
  GPR_ASSERT(grpc_call_start_write_old(call, byte_buffer, tag, flags) ==
             GRPC_CALL_OK);
  grpc_byte_buffer_destroy(byte_buffer);
}

grpc_completion_type grpc_event_type(const grpc_event *event) {
  return event->type;
}

grpc_op_error grpc_event_write_accepted(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_WRITE_ACCEPTED);
  return event->data.invoke_accepted;
}

grpc_op_error grpc_event_finish_accepted(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISH_ACCEPTED);
  return event->data.finish_accepted;
}

grpc_status_code grpc_event_finished_status(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISHED);
  return event->data.finished.status;
}

const char *grpc_event_finished_details(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISHED);
  return event->data.finished.details;
}

gpr_intptr grpc_event_read_length(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_READ);
  if (!event->data.read) {
    return -1;
  }
  return grpc_byte_buffer_length(event->data.read);
}

/*
 * Copies data from read event to a buffer. Fatal error occurs if
 * buffer is too small.
 */
void grpc_event_read_copy_to_buffer(const grpc_event *event, char *buffer,
                                    size_t buffer_len) {
  grpc_byte_buffer_reader *reader;
  gpr_slice slice;
  size_t offset = 0;

  GPR_ASSERT(event->type == GRPC_READ);
  reader = grpc_byte_buffer_reader_create(event->data.read);

  GPR_ASSERT(event->data.read);
  while (grpc_byte_buffer_reader_next(reader, &slice)) {
    size_t len = GPR_SLICE_LENGTH(slice);
    GPR_ASSERT(offset + len <= buffer_len);
    memcpy(buffer + offset, GPR_SLICE_START_PTR(slice),
           GPR_SLICE_LENGTH(slice));
    offset += len;
    gpr_slice_unref(slice);
  }
  grpc_byte_buffer_reader_destroy(reader);
}

grpc_call *grpc_event_call(const grpc_event *event) {
  /* we only allow this for newly incoming server calls. */
  GPR_ASSERT(event->type == GRPC_SERVER_RPC_NEW);
  return event->call;
}

const char *grpc_event_server_rpc_new_method(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_SERVER_RPC_NEW);
  return event->data.server_rpc_new.method;
}

grpc_completion_type grpc_completion_queue_next_with_callback(
    grpc_completion_queue *cq) {
  grpc_event *ev;
  grpc_completion_type t;
  void (*callback)(grpc_event *);

  ev = grpc_completion_queue_next(cq, gpr_inf_future);
  t = ev->type;
  if (ev->tag) {
    /* call the callback in ev->tag */
    /* C forbids to cast object pointers to function pointers, so
     * we cast to intptr first.
     */
    callback = (void (*)(grpc_event *))(gpr_intptr)ev->tag;
    (*callback)(ev);
  }
  grpc_event_finish(ev);

  /* return completion type to allow some handling for events that have no
   * tag - such as GRPC_QUEUE_SHUTDOWN
   */
  return t;
}
