//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h"

#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/useful.h"

const size_t kInitialIovecBufferSize = 8;

// Makes sure iovec_buf in alts_grpc_record_protocol is large enough.
static void ensure_iovec_buf_size(alts_grpc_record_protocol* rp,
                                  const grpc_slice_buffer* sb) {
  CHECK(rp != nullptr);
  CHECK_NE(sb, nullptr);
  if (sb->count <= rp->iovec_buf_length) {
    return;
  }
  // At least double the iovec buffer size.
  rp->iovec_buf_length = std::max(sb->count, 2 * rp->iovec_buf_length);
  rp->iovec_buf = static_cast<iovec_t*>(
      gpr_realloc(rp->iovec_buf, rp->iovec_buf_length * sizeof(iovec_t)));
}

// --- Implementation of methods defined in tsi_grpc_record_protocol_common.h.
// ---

void alts_grpc_record_protocol_convert_slice_buffer_to_iovec(
    alts_grpc_record_protocol* rp, const grpc_slice_buffer* sb) {
  CHECK(rp != nullptr);
  CHECK_NE(sb, nullptr);
  ensure_iovec_buf_size(rp, sb);
  for (size_t i = 0; i < sb->count; i++) {
    rp->iovec_buf[i].iov_base = GRPC_SLICE_START_PTR(sb->slices[i]);
    rp->iovec_buf[i].iov_len = GRPC_SLICE_LENGTH(sb->slices[i]);
  }
}

void alts_grpc_record_protocol_copy_slice_buffer(const grpc_slice_buffer* src,
                                                 unsigned char* dst) {
  CHECK(src != nullptr);
  CHECK_NE(dst, nullptr);
  for (size_t i = 0; i < src->count; i++) {
    size_t slice_length = GRPC_SLICE_LENGTH(src->slices[i]);
    memcpy(dst, GRPC_SLICE_START_PTR(src->slices[i]), slice_length);
    dst += slice_length;
  }
}

iovec_t alts_grpc_record_protocol_get_header_iovec(
    alts_grpc_record_protocol* rp) {
  iovec_t header_iovec = {nullptr, 0};
  if (rp == nullptr) {
    return header_iovec;
  }
  header_iovec.iov_len = rp->header_length;
  if (rp->header_sb.count == 1) {
    header_iovec.iov_base = GRPC_SLICE_START_PTR(rp->header_sb.slices[0]);
  } else {
    // Frame header is in multiple slices, copies the header bytes from slice
    // buffer to a single flat buffer.
    alts_grpc_record_protocol_copy_slice_buffer(&rp->header_sb, rp->header_buf);
    header_iovec.iov_base = rp->header_buf;
  }
  return header_iovec;
}

tsi_result alts_grpc_record_protocol_init(alts_grpc_record_protocol* rp,
                                          gsec_aead_crypter* crypter,
                                          size_t overflow_size, bool is_client,
                                          bool is_integrity_only,
                                          bool is_protect) {
  if (rp == nullptr || crypter == nullptr) {
    LOG(ERROR)
        << "Invalid nullptr arguments to alts_grpc_record_protocol init.";
    return TSI_INVALID_ARGUMENT;
  }
  // Creates alts_iovec_record_protocol.
  char* error_details = nullptr;
  grpc_status_code status = alts_iovec_record_protocol_create(
      crypter, overflow_size, is_client, is_integrity_only, is_protect,
      &rp->iovec_rp, &error_details);
  if (status != GRPC_STATUS_OK) {
    LOG(ERROR) << "Failed to create alts_iovec_record_protocol, "
               << error_details;
    gpr_free(error_details);
    return TSI_INTERNAL_ERROR;
  }
  // Allocates header slice buffer.
  grpc_slice_buffer_init(&rp->header_sb);
  // Allocates header buffer.
  rp->header_length = alts_iovec_record_protocol_get_header_length();
  rp->header_buf = static_cast<unsigned char*>(gpr_malloc(rp->header_length));
  rp->tag_length = alts_iovec_record_protocol_get_tag_length(rp->iovec_rp);
  // Allocates iovec buffer.
  rp->iovec_buf_length = kInitialIovecBufferSize;
  rp->iovec_buf =
      static_cast<iovec_t*>(gpr_malloc(rp->iovec_buf_length * sizeof(iovec_t)));
  return TSI_OK;
}

// --- Implementation of methods defined in tsi_grpc_record_protocol.h. ---
tsi_result alts_grpc_record_protocol_protect(
    alts_grpc_record_protocol* self, grpc_slice_buffer* unprotected_slices,
    grpc_slice_buffer* protected_slices) {
  if (self == nullptr || self->vtable == nullptr ||
      unprotected_slices == nullptr || protected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->protect == nullptr) {
    return TSI_UNIMPLEMENTED;
  }
  return self->vtable->protect(self, unprotected_slices, protected_slices);
}

tsi_result alts_grpc_record_protocol_unprotect(
    alts_grpc_record_protocol* self, grpc_slice_buffer* protected_slices,
    grpc_slice_buffer* unprotected_slices) {
  if (self == nullptr || self->vtable == nullptr ||
      protected_slices == nullptr || unprotected_slices == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  if (self->vtable->unprotect == nullptr) {
    return TSI_UNIMPLEMENTED;
  }
  return self->vtable->unprotect(self, protected_slices, unprotected_slices);
}

void alts_grpc_record_protocol_destroy(alts_grpc_record_protocol* self) {
  if (self == nullptr) {
    return;
  }
  if (self->vtable->destruct != nullptr) {
    self->vtable->destruct(self);
  }
  alts_iovec_record_protocol_destroy(self->iovec_rp);
  grpc_slice_buffer_destroy(&self->header_sb);
  gpr_free(self->header_buf);
  gpr_free(self->iovec_buf);
  gpr_free(self);
}

// Integrity-only and privacy-integrity share the same implementation. No need
// to call vtable.
size_t alts_grpc_record_protocol_max_unprotected_data_size(
    const alts_grpc_record_protocol* self, size_t max_protected_frame_size) {
  if (self == nullptr) {
    return 0;
  }
  return alts_iovec_record_protocol_max_unprotected_data_size(
      self->iovec_rp, max_protected_frame_size);
}
