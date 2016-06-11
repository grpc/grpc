/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <cstdio>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include "test_config.h"

static void *tag(intptr_t i) { return (void *)i; }

int main(int argc, char **argv) {
    grpc_channel *chan;
    grpc_call *call;
    gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2);
    grpc_completion_queue *cq;
    grpc_op ops[6];
    grpc_op *op;
    grpc_metadata_array trailing_metadata_recv;
    grpc_status_code status;
    char *details = NULL;
    size_t details_capacity = 0;

    grpc_test_init(argc, argv);
    grpc_init();

    grpc_metadata_array_init(&trailing_metadata_recv);

    cq = grpc_completion_queue_create(NULL);

    /* create a call, channel to a non existant server */
    chan = grpc_insecure_channel_create("0.0.0.0:50051", NULL, NULL);
    call = grpc_channel_create_call(chan, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                                    "/helloworld.Greeter/SayHello", "0.0.0.0", deadline, NULL);

    op = ops;

    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    op++;

    op->op = GRPC_OP_SEND_MESSAGE;
    // hardcoded string for "gRPC-C"
    const char str[] = { 0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43 };
    gpr_slice slice = gpr_slice_from_copied_buffer(str, sizeof(str));
    op->data.send_message = grpc_raw_byte_buffer_create(&slice, 1);
    op->flags = 0;
    op->reserved = NULL;
    op++;

    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    grpc_metadata_array metadata_array;
    grpc_metadata_array_init(&metadata_array);
    op->data.recv_initial_metadata = &metadata_array;
    op->flags = 0;
    op->reserved = NULL;
    op++;

    op->op = GRPC_OP_RECV_MESSAGE;
    grpc_byte_buffer *buffer = NULL;
    op->data.recv_message = &buffer;
    op->flags = 0;
    op->reserved = NULL;
    op++;

    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = NULL;
    op++;

    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op->data.recv_status_on_client.status_details_capacity = &details_capacity;
    op->flags = 0;
    op->reserved = NULL;
    op++;

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(
            call, ops, (size_t)(op - ops), tag(1), NULL));

    /* wait till all tags get completed */
    for (;;) {
        grpc_event ev;
        ev = grpc_completion_queue_next(cq, deadline, NULL);
        if (ev.tag == tag(1)) break;
    }

    printf("%x\n", buffer);
    printf("%d\n", status);
    printf("%s\n", details);

    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, buffer);
    gpr_slice slice_recv = grpc_byte_buffer_reader_readall(&reader);
    uint8_t *response = GPR_SLICE_START_PTR(slice_recv);
    printf("Server said: %s", response + 2);    // skip to the string in serialized protobuf object
    GPR_ASSERT(status == GRPC_STATUS_OK);

    grpc_completion_queue_shutdown(cq);
    while (
            grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL)
                    .type != GRPC_QUEUE_SHUTDOWN)
        ;
    grpc_completion_queue_destroy(cq);
    grpc_call_destroy(call);
    grpc_channel_destroy(chan);

    gpr_free(details);
    grpc_metadata_array_destroy(&trailing_metadata_recv);

    grpc_shutdown();

    return 0;
}
