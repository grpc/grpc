/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/slice/slice_internal.h"

typedef struct call_data {
  grpc_call_combiner *call_combiner;
  // The original recv_initial_metadata_ready callback.
  grpc_closure *original_recv_initial_metadata_ready;
  // Used to both inject our recv_initial_metadata_ready callback and to
  // bounce back into the call combiner after calling out to the
  // application.
  grpc_closure recv_initial_metadata_ready;
  grpc_metadata_array md;
  const grpc_metadata *consumed_md;
  size_t num_consumed_md;
  grpc_auth_context *auth_context;
} call_data;

typedef struct channel_data {
  grpc_auth_context *auth_context;
  grpc_server_credentials *creds;
} channel_data;

static grpc_metadata_array metadata_batch_to_md_array(
    const grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  grpc_metadata_array result;
  grpc_metadata_array_init(&result);
  for (l = batch->list.head; l != NULL; l = l->next) {
    grpc_metadata *usr_md = NULL;
    grpc_mdelem md = l->md;
    grpc_slice key = GRPC_MDKEY(md);
    grpc_slice value = GRPC_MDVALUE(md);
    if (result.count == result.capacity) {
      result.capacity = GPR_MAX(result.capacity + 8, result.capacity * 2);
      result.metadata =
          gpr_realloc(result.metadata, result.capacity * sizeof(grpc_metadata));
    }
    usr_md = &result.metadata[result.count++];
    usr_md->key = grpc_slice_ref_internal(key);
    usr_md->value = grpc_slice_ref_internal(value);
  }
  return result;
}

static grpc_filtered_mdelem remove_consumed_md(grpc_exec_ctx *exec_ctx,
                                               void *user_data,
                                               grpc_mdelem md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;
  size_t i;
  for (i = 0; i < calld->num_consumed_md; i++) {
    const grpc_metadata *consumed_md = &calld->consumed_md[i];
    if (grpc_slice_eq(GRPC_MDKEY(md), consumed_md->key) &&
        grpc_slice_eq(GRPC_MDVALUE(md), consumed_md->value))
      return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

// Called from application code.
// Note that this does NOT execute inside of the call combiner, but that
// should be okay, because there can be only one recv_initial_metadata
// callback per call.
static void on_md_processing_done(
    void *user_data, const grpc_metadata *consumed_md, size_t num_consumed_md,
    const grpc_metadata *response_md, size_t num_response_md,
    grpc_status_code status, const char *error_details) {
  grpc_transport_stream_op_batch *batch = user_data;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  /* TODO(jboeuf): Implement support for response_md. */
  if (response_md != NULL && num_response_md > 0) {
    gpr_log(GPR_INFO,
            "response_md in auth metadata processing not supported for now. "
            "Ignoring...");
  }
  grpc_error *error = GRPC_ERROR_NONE;
  if (status == GRPC_STATUS_OK) {
    calld->consumed_md = consumed_md;
    calld->num_consumed_md = num_consumed_md;
    error = grpc_metadata_batch_filter(
        &exec_ctx, batch->payload->recv_initial_metadata.recv_initial_metadata,
        remove_consumed_md, elem, "Response metadata filtering error");
  } else {
    if (error_details == NULL) {
      error_details = "Authentication metadata processing failed.";
    }
    error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_details),
        GRPC_ERROR_INT_GRPC_STATUS, status);
  }
  for (size_t i = 0; i < calld->md.count; i++) {
    grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].key);
    grpc_slice_unref_internal(&exec_ctx, calld->md.metadata[i].value);
  }
  grpc_metadata_array_destroy(&calld->md);
  // This callback was changed earlier to re-enter the call combiner.
  GRPC_CLOSURE_SCHED(&exec_ctx, &calld->recv_initial_metadata_ready, error);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void run_in_call_combiner(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  grpc_closure *closure = arg;
  GRPC_CLOSURE_RUN(exec_ctx, closure, GRPC_ERROR_REF(error));
}

static void recv_initial_metadata_ready(grpc_exec_ctx *exec_ctx,
                                        void *arg, grpc_error *error) {
  grpc_transport_stream_op_batch *batch = arg;
  grpc_call_element *elem = batch->handler_private.extra_arg;
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (error == GRPC_ERROR_NONE) {
    if (chand->creds != NULL && chand->creds->processor.process != NULL) {
      // We're calling out to the application, so we need to exit and
      // then re-enter the call combiner.
      // Note that we reuse calld->recv_initial_metadata_ready here,
      // which is fine, because it is no longer used once this function
      // is called.
gpr_log(GPR_INFO, "INTERCEPTING recv_initial_metadata CLOSURE ON call_combiner=%p", calld->call_combiner);
      GRPC_CLOSURE_INIT(&calld->recv_initial_metadata_ready,
                        run_in_call_combiner,
                        calld->original_recv_initial_metadata_ready,
                        &calld->call_combiner->scheduler);
      calld->md = metadata_batch_to_md_array(
          batch->payload->recv_initial_metadata.recv_initial_metadata);
      chand->creds->processor.process(
          chand->creds->processor.state, calld->auth_context,
          calld->md.metadata, calld->md.count, on_md_processing_done, batch);
gpr_log(GPR_INFO, "STOPPING call_combiner=%p", calld->call_combiner);
      grpc_call_combiner_stop(exec_ctx, calld->call_combiner);
      return;
    }
  }
  GRPC_CLOSURE_RUN(exec_ctx, calld->original_recv_initial_metadata_ready,
                   GRPC_ERROR_REF(error));
}

static void auth_start_transport_stream_op_batch(
    grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
    grpc_transport_stream_op_batch *batch) {
  call_data *calld = elem->call_data;
  if (batch->recv_initial_metadata) {
    // Inject our callback.
    calld->original_recv_initial_metadata_ready =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->handler_private.extra_arg = elem;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        GRPC_CLOSURE_INIT(&calld->recv_initial_metadata_ready,
                          recv_initial_metadata_ready, batch,
                          grpc_schedule_on_exec_ctx);
  }
  grpc_call_next_op(exec_ctx, elem, batch);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  calld->call_combiner = args->call_combiner;
  // Create server security context.  Set its auth context from channel
  // data and save it in the call context.
  grpc_server_security_context *server_ctx =
      grpc_server_security_context_create();
  server_ctx->auth_context = grpc_auth_context_create(chand->auth_context);
  calld->auth_context = server_ctx->auth_context;
  if (args->context[GRPC_CONTEXT_SECURITY].value != NULL) {
    args->context[GRPC_CONTEXT_SECURITY].destroy(
        args->context[GRPC_CONTEXT_SECURITY].value);
  }
  args->context[GRPC_CONTEXT_SECURITY].value = server_ctx;
  args->context[GRPC_CONTEXT_SECURITY].destroy =
      grpc_server_security_context_destroy;
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);
  channel_data *chand = elem->channel_data;
  grpc_auth_context *auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  GPR_ASSERT(auth_context != NULL);
  chand->auth_context =
      GRPC_AUTH_CONTEXT_REF(auth_context, "server_auth_filter");
  grpc_server_credentials *creds =
      grpc_find_server_credentials_in_args(args->channel_args);
  chand->creds = grpc_server_credentials_ref(creds);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  GRPC_AUTH_CONTEXT_UNREF(chand->auth_context, "server_auth_filter");
  grpc_server_credentials_unref(exec_ctx, chand->creds);
}

const grpc_channel_filter grpc_server_auth_filter = {
    auth_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    grpc_channel_next_get_info,
    "server-auth"};
