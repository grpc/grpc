/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/tsi/transport_security_adapter.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/tsi/transport_security.h"

#define TSI_ADAPTER_INITIAL_BUFFER_SIZE 256

/* --- tsi_adapter_handshaker_result implementation ---*/

typedef struct {
  tsi_handshaker_result base;
  tsi_handshaker *wrapped;
  unsigned char *unused_bytes;
  size_t unused_bytes_size;
} tsi_adapter_handshaker_result;

static tsi_result adapter_result_extract_peer(const tsi_handshaker_result *self,
                                              tsi_peer *peer) {
  tsi_adapter_handshaker_result *impl = (tsi_adapter_handshaker_result *)self;
  return tsi_handshaker_extract_peer(impl->wrapped, peer);
}

static tsi_result adapter_result_create_frame_protector(
    const tsi_handshaker_result *self, size_t *max_output_protected_frame_size,
    tsi_frame_protector **protector) {
  tsi_adapter_handshaker_result *impl = (tsi_adapter_handshaker_result *)self;
  return tsi_handshaker_create_frame_protector(
      impl->wrapped, max_output_protected_frame_size, protector);
}

static tsi_result adapter_result_get_unused_bytes(
    const tsi_handshaker_result *self, const unsigned char **bytes,
    size_t *byte_size) {
  tsi_adapter_handshaker_result *impl = (tsi_adapter_handshaker_result *)self;
  *bytes = impl->unused_bytes;
  *byte_size = impl->unused_bytes_size;
  return TSI_OK;
}

static void adapter_result_destroy(tsi_handshaker_result *self) {
  tsi_adapter_handshaker_result *impl = (tsi_adapter_handshaker_result *)self;
  tsi_handshaker_destroy(impl->wrapped);
  gpr_free(impl->unused_bytes);
  gpr_free(self);
}

static const tsi_handshaker_result_vtable result_vtable = {
    adapter_result_extract_peer,
    NULL, /* create_zero_copy_grpc_protector */
    adapter_result_create_frame_protector,
    adapter_result_get_unused_bytes,
    adapter_result_destroy,
};

/* Ownership of wrapped tsi_handshaker is transferred to the result object.  */
static tsi_result tsi_adapter_create_handshaker_result(
    tsi_handshaker *wrapped, const unsigned char *unused_bytes,
    size_t unused_bytes_size, tsi_handshaker_result **handshaker_result) {
  if (wrapped == NULL || (unused_bytes_size > 0 && unused_bytes == NULL)) {
    return TSI_INVALID_ARGUMENT;
  }
  tsi_adapter_handshaker_result *impl = gpr_zalloc(sizeof(*impl));
  impl->base.vtable = &result_vtable;
  impl->wrapped = wrapped;
  impl->unused_bytes_size = unused_bytes_size;
  if (unused_bytes_size > 0) {
    impl->unused_bytes = gpr_malloc(unused_bytes_size);
    memcpy(impl->unused_bytes, unused_bytes, unused_bytes_size);
  } else {
    impl->unused_bytes = NULL;
  }
  *handshaker_result = &impl->base;
  return TSI_OK;
}

/* --- tsi_adapter_handshaker implementation ---*/

typedef struct {
  tsi_handshaker base;
  tsi_handshaker *wrapped;
  unsigned char *adapter_buffer;
  size_t adapter_buffer_size;
} tsi_adapter_handshaker;

static tsi_result adapter_get_bytes_to_send_to_peer(tsi_handshaker *self,
                                                    unsigned char *bytes,
                                                    size_t *bytes_size) {
  return tsi_handshaker_get_bytes_to_send_to_peer(
      tsi_adapter_handshaker_get_wrapped(self), bytes, bytes_size);
}

static tsi_result adapter_process_bytes_from_peer(tsi_handshaker *self,
                                                  const unsigned char *bytes,
                                                  size_t *bytes_size) {
  return tsi_handshaker_process_bytes_from_peer(
      tsi_adapter_handshaker_get_wrapped(self), bytes, bytes_size);
}

static tsi_result adapter_get_result(tsi_handshaker *self) {
  return tsi_handshaker_get_result(tsi_adapter_handshaker_get_wrapped(self));
}

static tsi_result adapter_extract_peer(tsi_handshaker *self, tsi_peer *peer) {
  return tsi_handshaker_extract_peer(tsi_adapter_handshaker_get_wrapped(self),
                                     peer);
}

static tsi_result adapter_create_frame_protector(
    tsi_handshaker *self, size_t *max_protected_frame_size,
    tsi_frame_protector **protector) {
  return tsi_handshaker_create_frame_protector(
      tsi_adapter_handshaker_get_wrapped(self), max_protected_frame_size,
      protector);
}

static void adapter_destroy(tsi_handshaker *self) {
  tsi_adapter_handshaker *impl = (tsi_adapter_handshaker *)self;
  tsi_handshaker_destroy(impl->wrapped);
  gpr_free(impl->adapter_buffer);
  gpr_free(self);
}

static tsi_result adapter_next(
    tsi_handshaker *self, const unsigned char *received_bytes,
    size_t received_bytes_size, const unsigned char **bytes_to_send,
    size_t *bytes_to_send_size, tsi_handshaker_result **handshaker_result,
    tsi_handshaker_on_next_done_cb cb, void *user_data) {
  /* Input sanity check.  */
  if ((received_bytes_size > 0 && received_bytes == NULL) ||
      bytes_to_send == NULL || bytes_to_send_size == NULL ||
      handshaker_result == NULL) {
    return TSI_INVALID_ARGUMENT;
  }

  /* If there are received bytes, process them first.  */
  tsi_adapter_handshaker *impl = (tsi_adapter_handshaker *)self;
  tsi_result status = TSI_OK;
  size_t bytes_consumed = received_bytes_size;
  if (received_bytes_size > 0) {
    status = tsi_handshaker_process_bytes_from_peer(
        impl->wrapped, received_bytes, &bytes_consumed);
    if (status != TSI_OK) return status;
  }

  /* Get bytes to send to the peer, if available.  */
  size_t offset = 0;
  do {
    size_t to_send_size = impl->adapter_buffer_size - offset;
    status = tsi_handshaker_get_bytes_to_send_to_peer(
        impl->wrapped, impl->adapter_buffer + offset, &to_send_size);
    offset += to_send_size;
    if (status == TSI_INCOMPLETE_DATA) {
      impl->adapter_buffer_size *= 2;
      impl->adapter_buffer =
          gpr_realloc(impl->adapter_buffer, impl->adapter_buffer_size);
    }
  } while (status == TSI_INCOMPLETE_DATA);
  if (status != TSI_OK) return status;
  *bytes_to_send = impl->adapter_buffer;
  *bytes_to_send_size = offset;

  /* If handshake completes, create tsi_handshaker_result.  */
  if (tsi_handshaker_is_in_progress(impl->wrapped)) {
    *handshaker_result = NULL;
  } else {
    size_t unused_bytes_size = received_bytes_size - bytes_consumed;
    const unsigned char *unused_bytes =
        unused_bytes_size == 0 ? NULL : received_bytes + bytes_consumed;
    status = tsi_adapter_create_handshaker_result(
        impl->wrapped, unused_bytes, unused_bytes_size, handshaker_result);
    if (status == TSI_OK) {
      impl->base.handshaker_result_created = true;
      impl->wrapped = NULL;
    }
  }
  return status;
}

static const tsi_handshaker_vtable handshaker_vtable = {
    adapter_get_bytes_to_send_to_peer,
    adapter_process_bytes_from_peer,
    adapter_get_result,
    adapter_extract_peer,
    adapter_create_frame_protector,
    adapter_destroy,
    adapter_next,
};

tsi_handshaker *tsi_create_adapter_handshaker(tsi_handshaker *wrapped) {
  GPR_ASSERT(wrapped != NULL);
  tsi_adapter_handshaker *impl = gpr_zalloc(sizeof(*impl));
  impl->base.vtable = &handshaker_vtable;
  impl->wrapped = wrapped;
  impl->adapter_buffer_size = TSI_ADAPTER_INITIAL_BUFFER_SIZE;
  impl->adapter_buffer = gpr_malloc(impl->adapter_buffer_size);
  return &impl->base;
}

tsi_handshaker *tsi_adapter_handshaker_get_wrapped(tsi_handshaker *adapter) {
  if (adapter == NULL) return NULL;
  tsi_adapter_handshaker *impl = (tsi_adapter_handshaker *)adapter;
  return impl->wrapped;
}
